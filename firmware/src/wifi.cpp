// Joins the strongest saved network, retries, and runs control requests from the main loop so no CYW43 call happens inside the driver's own context.
#include "wifi.h"

#include <cstring>

#include "lwip/dns.h"
#include "lwip/netif.h"
#include "pico/cyw43_arch.h"
#include "pico/time.h"

#include "log.h"
#include "napt_lwip.h"
#include "settings.h"

namespace wifi {

namespace {

constexpr uint8_t security_wep = 1;
constexpr uint8_t security_wpa = 2;
constexpr uint8_t security_wpa2 = 4;
constexpr uint32_t connect_timeout_ms = 20000;
constexpr uint32_t retry_delay_ms = 30000;
constexpr uint32_t reconnect_delay_ms = 2000;
constexpr size_t auth_attempt_limit = 2;

enum class State { idle, scanning, connecting, connected, failed, not_found, disconnected };

State state = State::idle;
uint32_t next_action_ms = 0;
uint32_t connect_started_ms = 0;
bool scan_for_connect = false;

AccessPoint scanned[scan_limit];
AccessPoint collecting[scan_limit];
size_t scanned_count = 0;
size_t collecting_count = 0;
bool scanning = false;
uint32_t generation = 0;

settings::Network target = {};
enum class Action { none, join, connect, disconnect, forget };
Action pending_action = Action::none;
settings::Network pending_network = {};
bool pending_scan = false;
constexpr uint32_t radio_refresh_ms = 2000;
uint32_t last_radio_refresh = 0;
int32_t cached_rssi = 0;
bool cached_bssid_valid = false;
uint8_t cached_bssid[6] = {};
bool cached_mac_valid = false;
uint8_t cached_mac[6] = {};
uint32_t auth_attempts[auth_attempt_limit];
size_t auth_attempt_count = 0;
size_t auth_attempt_index = 0;

netif *station() {
    return &cyw43_state.netif[CYW43_ITF_STA];
}

int scan_result_received(void *, const cyw43_ev_scan_result_t *result) {
    if (!result->ssid_len || result->ssid_len > 32) {
        return 0;
    }
    for (size_t index = 0; index < collecting_count; index++) {
        AccessPoint &existing = collecting[index];
        if (existing.ssid_length == result->ssid_len && !memcmp(existing.ssid, result->ssid, result->ssid_len)) {
            if (result->rssi > existing.rssi) {
                existing.rssi = result->rssi;
                existing.security = result->auth_mode;
                existing.channel = static_cast<uint8_t>(result->channel);
            }
            return 0;
        }
    }
    if (collecting_count == scan_limit) {
        return 0;
    }
    AccessPoint &entry = collecting[collecting_count++];
    entry.ssid_length = result->ssid_len;
    memcpy(entry.ssid, result->ssid, result->ssid_len);
    entry.rssi = result->rssi;
    entry.security = result->auth_mode;
    entry.channel = static_cast<uint8_t>(result->channel);
    return 0;
}

void finish_scan() {
    for (size_t outer = 1; outer < collecting_count; outer++) {
        AccessPoint moving = collecting[outer];
        size_t inner = outer;
        while (inner > 0 && collecting[inner - 1].rssi < moving.rssi) {
            collecting[inner] = collecting[inner - 1];
            inner--;
        }
        collecting[inner] = moving;
    }
    memcpy(scanned, collecting, sizeof(AccessPoint) * collecting_count);
    scanned_count = collecting_count;
    scanning = false;
    generation++;
    log_line("wifi: scan found %u networks%s", static_cast<unsigned>(scanned_count), state == State::connected ? " while connected" : "");
}

const AccessPoint *scanned_entry(const uint8_t *ssid, size_t length) {
    for (size_t index = 0; index < scanned_count; index++) {
        if (scanned[index].ssid_length == length && !memcmp(scanned[index].ssid, ssid, length)) {
            return &scanned[index];
        }
    }
    return nullptr;
}

void plan_auth(const settings::Network &network, const AccessPoint *seen) {
    auth_attempt_count = 0;
    auth_attempt_index = 0;
    if (!network.password_length || (seen && seen->security == 0)) {
        auth_attempts[auth_attempt_count++] = CYW43_AUTH_OPEN;
        return;
    }
    if (seen && seen->security == security_wpa) {
        auth_attempts[auth_attempt_count++] = CYW43_AUTH_WPA_TKIP_PSK;
        return;
    }
    auth_attempts[auth_attempt_count++] = CYW43_AUTH_WPA2_MIXED_PSK;
    auth_attempts[auth_attempt_count++] = CYW43_AUTH_WPA3_WPA2_AES_PSK;
}

void set_outside(uint32_t address) {
    napt_set_outside_address(address);
}

void start_attempt(uint32_t now) {
    char ssid[33] = {};
    char password[65] = {};
    memcpy(ssid, target.ssid, target.ssid_length);
    memcpy(password, target.password, target.password_length);
    uint32_t auth = auth_attempts[auth_attempt_index];
    log_line("wifi: connecting to %s (auth %08lx)", ssid, static_cast<unsigned long>(auth));
    int result = cyw43_arch_wifi_connect_async(ssid, auth == CYW43_AUTH_OPEN ? nullptr : password, auth);
    memset(password, 0, sizeof(password));
    state = State::connecting;
    connect_started_ms = now;
    if (result) {
        log_line("wifi: connect request failed (%d)", result);
        state = State::failed;
        next_action_ms = now + retry_delay_ms;
    }
}

void connect_to(const settings::Network &network, uint32_t now) {
    cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
    set_outside(0);
    target = network;
    plan_auth(target, scanned_entry(target.ssid, target.ssid_length));
    start_attempt(now);
}

void choose_known_network(uint32_t now) {
    for (size_t index = 0; index < scanned_count; index++) {
        int known = settings::find(scanned[index].ssid, scanned[index].ssid_length);
        if (known >= 0) {
            connect_to(settings::network(static_cast<size_t>(known)), now);
            return;
        }
    }
    state = State::not_found;
    next_action_ms = now + retry_delay_ms;
    log_line("wifi: no saved network in range");
}

void attempt_failed(uint32_t now, int status) {
    log_line("wifi: connection failed (%d)", status);
    cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
    if (++auth_attempt_index < auth_attempt_count) {
        start_attempt(now);
        return;
    }
    state = State::failed;
    next_action_ms = now + retry_delay_ms;
}

uint32_t host_order(const ip4_addr_t *address) {
    return lwip_ntohl(ip4_addr_get_u32(address));
}

bool begin_scan() {
    if (scanning) {
        return true;
    }
    cyw43_wifi_scan_options_t options = {};
    collecting_count = 0;
    if (cyw43_wifi_scan(&cyw43_state, &options, nullptr, scan_result_received)) {
        return false;
    }
    scanning = true;
    return true;
}

void refresh_radio(uint32_t now) {
    if (now - last_radio_refresh < radio_refresh_ms) {
        return;
    }
    last_radio_refresh = now;
    if (!cached_mac_valid) {
        cached_mac_valid = cyw43_wifi_get_mac(&cyw43_state, CYW43_ITF_STA, cached_mac) == 0;
    }
    if (state == State::connected) {
        int32_t value = 0;
        if (cyw43_wifi_get_rssi(&cyw43_state, &value) == 0) {
            cached_rssi = value;
        }
        cached_bssid_valid = cyw43_wifi_get_bssid(&cyw43_state, cached_bssid) == 0;
    } else {
        cached_rssi = 0;
        cached_bssid_valid = false;
    }
}

void run_disconnect() {
    cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
    set_outside(0);
    scan_for_connect = false;
    state = State::disconnected;
    log_line("wifi: disconnected by request");
}

bool run_forget(const uint8_t *ssid, size_t ssid_length) {
    bool removed = settings::forget(ssid, ssid_length);
    if ((state == State::connected || state == State::connecting) && target.ssid_length == ssid_length &&
        !memcmp(target.ssid, ssid, ssid_length)) {
        cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
        set_outside(0);
        state = settings::count() ? State::not_found : State::idle;
        next_action_ms = to_ms_since_boot(get_absolute_time()) + reconnect_delay_ms;
    }
    return removed;
}

void run_pending(uint32_t now) {
    Action action = pending_action;
    pending_action = Action::none;
    switch (action) {
    case Action::join:
        settings::remember(pending_network);
        scan_for_connect = false;
        connect_to(pending_network, now);
        break;
    case Action::connect:
        scan_for_connect = false;
        connect_to(pending_network, now);
        break;
    case Action::disconnect:
        run_disconnect();
        break;
    case Action::forget:
        run_forget(pending_network.ssid, pending_network.ssid_length);
        break;
    case Action::none:
        break;
    }
    memset(pending_network.password, 0, sizeof(pending_network.password));
    if (pending_scan) {
        pending_scan = false;
        if (!begin_scan()) {
            log_line("wifi: scan request failed");
        }
    }
}

}

void init() {
    cyw43_arch_enable_sta_mode();
    cyw43_wifi_pm(&cyw43_state, CYW43_PERFORMANCE_PM);
    state = settings::count() ? State::not_found : State::idle;
    next_action_ms = 0;
}

void task(uint32_t now) {
    run_pending(now);
    if (state != State::connecting && !scanning) {
        refresh_radio(now);
    }
    if (scanning && !cyw43_wifi_scan_active(&cyw43_state)) {
        finish_scan();
        if (scan_for_connect) {
            scan_for_connect = false;
            if (state == State::scanning) {
                choose_known_network(now);
            }
        }
    }

    switch (state) {
    case State::disconnected:
        break;
    case State::idle:
        if (settings::count()) {
            state = State::not_found;
            next_action_ms = now;
        }
        break;
    case State::not_found:
    case State::failed:
        if (!settings::count()) {
            state = State::idle;
        } else if ((int32_t)(now - next_action_ms) >= 0 && begin_scan()) {
            state = State::scanning;
            scan_for_connect = true;
        }
        break;
    case State::scanning:
        if (!scanning && !scan_for_connect) {
            choose_known_network(now);
        }
        break;
    case State::connecting: {
        int status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
        if (status == CYW43_LINK_UP) {
            state = State::connected;
            set_outside(host_order(netif_ip4_addr(station())));
            int index = settings::find(target.ssid, target.ssid_length);
            if (index > 0) {
                settings::promote(static_cast<size_t>(index));
            }
            log_line("wifi: connected, address %s", ip4addr_ntoa(netif_ip4_addr(station())));
        } else if (status == CYW43_LINK_FAIL || status == CYW43_LINK_BADAUTH || status == CYW43_LINK_NONET) {
            attempt_failed(now, status);
        } else if (now - connect_started_ms > connect_timeout_ms) {
            attempt_failed(now, status);
        }
        break;
    }
    case State::connected:
        if (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) != CYW43_LINK_UP) {
            log_line("wifi: link lost");
            set_outside(0);
            cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
            state = State::not_found;
            next_action_ms = now + reconnect_delay_ms;
        }
        break;
    }
}

const char *state_name() {
    switch (state) {
    case State::idle:
        return "idle";
    case State::scanning:
        return "scanning";
    case State::connecting:
        return "connecting";
    case State::connected:
        return "connected";
    case State::failed:
        return "failed";
    case State::not_found:
        return "not-found";
    case State::disconnected:
        return "disconnected";
    }
    return "idle";
}

bool connected() {
    return state == State::connected;
}

bool joining() {
    return state == State::connecting;
}

bool current_ssid(uint8_t *ssid, size_t &length) {
    if (state != State::connected && state != State::connecting && state != State::failed) {
        return false;
    }
    if (!target.ssid_length) {
        return false;
    }
    memcpy(ssid, target.ssid, target.ssid_length);
    length = target.ssid_length;
    return true;
}

const AccessPoint *current_access_point() {
    if (!target.ssid_length) {
        return nullptr;
    }
    return scanned_entry(target.ssid, target.ssid_length);
}

bool bssid(uint8_t *address) {
    if (state != State::connected || !cached_bssid_valid) {
        return false;
    }
    memcpy(address, cached_bssid, 6);
    return true;
}

bool mac_address(uint8_t *address) {
    if (!cached_mac_valid) {
        return false;
    }
    memcpy(address, cached_mac, 6);
    return true;
}

uint32_t netmask() {
    return connected() ? host_order(netif_ip4_netmask(station())) : 0;
}

int32_t rssi() {
    return state == State::connected ? cached_rssi : 0;
}

uint32_t address() {
    return connected() ? host_order(netif_ip4_addr(station())) : 0;
}

uint32_t gateway() {
    return connected() ? host_order(netif_ip4_gw(station())) : 0;
}

uint32_t dns_server() {
    const ip_addr_t *server = dns_getserver(0);
    return connected() && server ? host_order(ip_2_ip4(server)) : 0;
}

bool scan_running() {
    return scanning;
}

uint32_t scan_generation() {
    return generation;
}

size_t scan_count() {
    return scanned_count;
}

const AccessPoint &scan_result(size_t index) {
    return scanned[index];
}

const char *security_name(uint8_t security) {
    if (security & security_wpa2) {
        return "wpa2";
    }
    if (security & security_wpa) {
        return "wpa";
    }
    if (security & security_wep) {
        return "wep";
    }
    return "open";
}

bool start_scan() {
    if (!scanning) {
        pending_scan = true;
    }
    return true;
}

void join(const uint8_t *ssid, size_t ssid_length, const uint8_t *password, size_t password_length) {
    pending_network = {};
    pending_network.ssid_length = static_cast<uint8_t>(ssid_length);
    memcpy(pending_network.ssid, ssid, ssid_length);
    pending_network.password_length = static_cast<uint8_t>(password_length);
    memcpy(pending_network.password, password, password_length);
    pending_action = Action::join;
}

bool connect_saved(const uint8_t *ssid, size_t ssid_length) {
    int index = settings::find(ssid, ssid_length);
    if (index < 0) {
        return false;
    }
    pending_network = settings::network(static_cast<size_t>(index));
    pending_action = Action::connect;
    return true;
}

void disconnect() {
    pending_action = Action::disconnect;
}

bool forget(const uint8_t *ssid, size_t ssid_length) {
    if (settings::find(ssid, ssid_length) < 0) {
        return false;
    }
    pending_network = {};
    pending_network.ssid_length = static_cast<uint8_t>(ssid_length);
    memcpy(pending_network.ssid, ssid, ssid_length);
    pending_action = Action::forget;
    return true;
}

}
