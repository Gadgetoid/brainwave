// Line-based control protocol on port 2350: status, scan, join, connect, disconnect, forget, known, log, update and debug-forward.
#include "control.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "lwip/tcp.h"
#include "pico/time.h"

#include "battery.h"
#include "brain_link.h"
#include "dhcp_server.h"
#include "log.h"
#include "napt_lwip.h"
#include "settings.h"
#include "updater.h"
#include "wifi.h"

namespace {

constexpr uint16_t control_port = 2350;
constexpr size_t connection_limit = 2;
constexpr size_t request_bytes = 400;
constexpr size_t response_bytes = 5120;
constexpr uint8_t idle_polls = 20;

struct Connection {
    bool used;
    bool waiting_for_scan;
    bool closing;
    bool updating;
    uint32_t update_remaining;
    uint8_t idle;
    uint32_t scan_generation;
    tcp_pcb *pcb;
    size_t request_length;
    char request[request_bytes];
    size_t response_length;
    size_t response_sent;
    char response[response_bytes];
};

tcp_pcb *listener = nullptr;
Connection connections[connection_limit];

void append(Connection &connection, const char *format, ...) __attribute__((format(printf, 2, 3)));

void append(Connection &connection, const char *format, ...) {
    if (connection.response_length >= response_bytes - 1) {
        return;
    }
    va_list arguments;
    va_start(arguments, format);
    int written = vsnprintf(connection.response + connection.response_length, response_bytes - connection.response_length, format, arguments);
    va_end(arguments);
    if (written > 0) {
        connection.response_length += static_cast<size_t>(written);
        if (connection.response_length > response_bytes - 1) {
            connection.response_length = response_bytes - 1;
        }
    }
}

void append_hex(Connection &connection, const uint8_t *bytes, size_t length) {
    if (!length) {
        append(connection, "-");
        return;
    }
    for (size_t index = 0; index < length; index++) {
        append(connection, "%02x", bytes[index]);
    }
}

void append_address(Connection &connection, const char *name, uint32_t address) {
    if (!address) {
        append(connection, "%s=-\n", name);
        return;
    }
    append(connection, "%s=%lu.%lu.%lu.%lu\n", name, static_cast<unsigned long>(address >> 24), static_cast<unsigned long>((address >> 16) & 0xff),
           static_cast<unsigned long>((address >> 8) & 0xff), static_cast<unsigned long>(address & 0xff));
}

int hex_value(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

bool decode_hex(const char *text, uint8_t *bytes, size_t capacity, size_t &length) {
    length = 0;
    if (!strcmp(text, "-")) {
        return true;
    }
    size_t characters = strlen(text);
    if (characters % 2 || characters / 2 > capacity) {
        return false;
    }
    for (size_t index = 0; index < characters; index += 2) {
        int high = hex_value(text[index]);
        int low = hex_value(text[index + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        bytes[length++] = static_cast<uint8_t>(high << 4 | low);
    }
    return true;
}

void release(Connection &connection) {
    if (connection.pcb) {
        tcp_arg(connection.pcb, nullptr);
        tcp_recv(connection.pcb, nullptr);
        tcp_sent(connection.pcb, nullptr);
        tcp_err(connection.pcb, nullptr);
        tcp_poll(connection.pcb, nullptr, 0);
    }
    connection.used = false;
    connection.pcb = nullptr;
}

void close_connection(Connection &connection) {
    tcp_pcb *pcb = connection.pcb;
    release(connection);
    if (pcb && tcp_close(pcb) != ERR_OK) {
        tcp_abort(pcb);
    }
}

void pump(Connection &connection) {
    while (connection.response_sent < connection.response_length) {
        size_t space = tcp_sndbuf(connection.pcb);
        size_t remaining = connection.response_length - connection.response_sent;
        size_t chunk = remaining < space ? remaining : space;
        if (!chunk) {
            break;
        }
        if (tcp_write(connection.pcb, connection.response + connection.response_sent, static_cast<u16_t>(chunk), TCP_WRITE_FLAG_COPY) != ERR_OK) {
            break;
        }
        connection.response_sent += chunk;
    }
    tcp_output(connection.pcb);
    if (connection.closing && connection.response_sent == connection.response_length) {
        close_connection(connection);
    }
}

void finish(Connection &connection) {
    connection.closing = true;
    pump(connection);
}

void append_mac(Connection &connection, const char *name, const uint8_t *address, bool valid) {
    if (!valid) {
        append(connection, "%s=-\n", name);
        return;
    }
    append(connection, "%s=%02x:%02x:%02x:%02x:%02x:%02x\n", name, address[0], address[1], address[2], address[3], address[4], address[5]);
}

void write_scan(Connection &connection) {
    for (size_t index = 0; index < wifi::scan_count(); index++) {
        const wifi::AccessPoint &entry = wifi::scan_result(index);
        bool saved = settings::find(entry.ssid, entry.ssid_length) >= 0;
        append(connection, "ap %d %s %u %d ", entry.rssi, wifi::security_name(entry.security), entry.channel, saved ? 1 : 0);
        append_hex(connection, entry.ssid, entry.ssid_length);
        append(connection, "\n");
    }
    append(connection, "ok\n");
}

void write_status(Connection &connection) {
    append(connection, "version=%s\n", BRAINWAVE_VERSION);
    append(connection, "wifi=%s\n", wifi::state_name());
    uint8_t ssid[32];
    size_t ssid_length = 0;
    append(connection, "ssid=");
    if (wifi::current_ssid(ssid, ssid_length)) {
        append_hex(connection, ssid, ssid_length);
    } else {
        append(connection, "-");
    }
    append(connection, "\n");
    const wifi::AccessPoint *access_point = wifi::current_access_point();
    if (access_point && ssid_length) {
        append(connection, "security=%s\nchannel=%u\n", wifi::security_name(access_point->security), access_point->channel);
    } else {
        append(connection, "security=-\nchannel=-\n");
    }
    if (wifi::connected()) {
        append(connection, "rssi=%ld\n", static_cast<long>(wifi::rssi()));
    } else {
        append(connection, "rssi=-\n");
    }
    uint8_t address[6];
    append_mac(connection, "bssid", address, wifi::bssid(address));
    append_address(connection, "ip", wifi::address());
    append_address(connection, "netmask", wifi::netmask());
    append_address(connection, "gateway", wifi::gateway());
    append_address(connection, "dns", wifi::dns_server());
    append_address(connection, "brain", dhcp_server_leased_address());
    append_mac(connection, "mac", address, wifi::mac_address(address));
    append(connection, "battery=%lu\n", static_cast<unsigned long>(battery_millivolts()));
    append(connection, "uptime=%lu\n", static_cast<unsigned long>(to_ms_since_boot(get_absolute_time()) / 1000));
    append(connection, "known=%u\n", static_cast<unsigned>(settings::count()));
    append(connection, "debug-forward=%s\n", napt_debug_forward() ? "on" : "off");
    append(connection, "slot=%s\n", updater_slot_name());
    append(connection, "ok\n");
}

void handle_command(Connection &connection) {
    char *words[4] = {};
    size_t word_count = 0;
    char *cursor = connection.request;
    while (*cursor && word_count < 4) {
        while (*cursor == ' ') {
            *cursor++ = '\0';
        }
        if (!*cursor) {
            break;
        }
        words[word_count++] = cursor;
        while (*cursor && *cursor != ' ') {
            cursor++;
        }
    }
    const char *command = word_count ? words[0] : "";

    if (!strcmp(command, "status")) {
        write_status(connection);
    } else if (!strcmp(command, "scan")) {
        connection.scan_generation = wifi::scan_generation();
        if (!wifi::start_scan()) {
            append(connection, "error scan failed\n");
        } else {
            connection.waiting_for_scan = true;
            return;
        }
    } else if (!strcmp(command, "join") && word_count == 3) {
        uint8_t ssid[settings::ssid_bytes];
        uint8_t password[settings::password_bytes];
        size_t ssid_length = 0;
        size_t password_length = 0;
        if (!decode_hex(words[1], ssid, sizeof(ssid), ssid_length) || !ssid_length ||
            !decode_hex(words[2], password, sizeof(password), password_length) || (password_length && password_length < 8)) {
            append(connection, "error bad network or password\n");
        } else {
            wifi::join(ssid, ssid_length, password, password_length);
            memset(password, 0, sizeof(password));
            append(connection, "ok\n");
        }
    } else if (!strcmp(command, "connect") && word_count == 2) {
        uint8_t ssid[settings::ssid_bytes];
        size_t ssid_length = 0;
        if (!decode_hex(words[1], ssid, sizeof(ssid), ssid_length) || !ssid_length) {
            append(connection, "error bad network\n");
        } else if (!wifi::connect_saved(ssid, ssid_length)) {
            append(connection, "error not saved\n");
        } else {
            append(connection, "ok\n");
        }
    } else if (!strcmp(command, "forget") && word_count == 2) {
        uint8_t ssid[settings::ssid_bytes];
        size_t ssid_length = 0;
        if (!decode_hex(words[1], ssid, sizeof(ssid), ssid_length) || !ssid_length) {
            append(connection, "error bad network\n");
        } else if (!wifi::forget(ssid, ssid_length)) {
            append(connection, "error not saved\n");
        } else {
            append(connection, "ok\n");
        }
    } else if (!strcmp(command, "debug-forward") && word_count == 2 && (!strcmp(words[1], "on") || !strcmp(words[1], "off"))) {
        napt_set_debug_forward(!strcmp(words[1], "on"));
        log_line("control: debug forward %s", words[1]);
        append(connection, "ok\n");
    } else if (!strcmp(command, "update") && word_count == 3) {
        char *end = nullptr;
        unsigned long size = strtoul(words[1], &end, 10);
        uint8_t hash[32];
        size_t hash_length = 0;
        const char *error = nullptr;
        if (!end || *end || !decode_hex(words[2], hash, sizeof(hash), hash_length) || hash_length != sizeof(hash)) {
            append(connection, "error bad size or hash\n");
        } else if (!updater_begin(static_cast<uint32_t>(size), hash, error)) {
            append(connection, "error %s\n", error);
        } else {
            connection.updating = true;
            connection.update_remaining = static_cast<uint32_t>(size);
            append(connection, "ready\n");
            pump(connection);
            return;
        }
    } else if (!strcmp(command, "disconnect") && word_count == 1) {
        wifi::disconnect();
        append(connection, "ok\n");
    } else if (!strcmp(command, "known")) {
        for (size_t index = 0; index < settings::count(); index++) {
            const settings::Network &network = settings::network(index);
            append(connection, "net ");
            append_hex(connection, network.ssid, network.ssid_length);
            append(connection, "\n");
        }
        append(connection, "ok\n");
    } else if (!strcmp(command, "log")) {
        size_t space = response_bytes - connection.response_length - 8;
        connection.response_length += log_copy(connection.response + connection.response_length, space);
        append(connection, "ok\n");
    } else {
        append(connection, "error unknown command\n");
    }
    finish(connection);
}

err_t on_sent(void *argument, tcp_pcb *, u16_t) {
    Connection *connection = static_cast<Connection *>(argument);
    if (connection) {
        connection->idle = 0;
        pump(*connection);
    }
    return ERR_OK;
}

void finish_update(Connection &connection) {
    const char *error = nullptr;
    connection.updating = false;
    if (updater_finish(error)) {
        append(connection, "ok\n");
    } else {
        append(connection, "error %s\n", error);
    }
    finish(connection);
}

void feed_update(Connection &connection, const uint8_t *bytes, size_t length) {
    if (!connection.updating || !length) {
        return;
    }
    if (length > connection.update_remaining) {
        length = connection.update_remaining;
    }
    const char *error = nullptr;
    if (!updater_write(bytes, length, error)) {
        connection.updating = false;
        append(connection, "error %s\n", error);
        finish(connection);
        return;
    }
    connection.update_remaining -= static_cast<uint32_t>(length);
    if (!connection.update_remaining) {
        finish_update(connection);
    }
}

err_t on_receive(void *argument, tcp_pcb *pcb, pbuf *packet, err_t error) {
    Connection *connection = static_cast<Connection *>(argument);
    if (!connection) {
        if (packet) {
            pbuf_free(packet);
        }
        return ERR_OK;
    }
    if (!packet) {
        if (connection->updating) {
            updater_abort();
        }
        close_connection(*connection);
        return ERR_OK;
    }
    connection->idle = 0;
    tcp_recved(pcb, packet->tot_len);
    for (pbuf *part = packet; part && connection->used; part = part->next) {
        const uint8_t *bytes = static_cast<const uint8_t *>(part->payload);
        u16_t index = 0;
        while (index < part->len && connection->used) {
            if (connection->updating) {
                feed_update(*connection, bytes + index, part->len - index);
                break;
            }
            if (connection->closing || connection->waiting_for_scan) {
                break;
            }
            char character = static_cast<char>(bytes[index++]);
            if (character == '\n') {
                connection->request[connection->request_length] = '\0';
                handle_command(*connection);
                continue;
            }
            if (character != '\r' && connection->request_length < request_bytes - 1) {
                connection->request[connection->request_length++] = character;
            }
        }
    }
    pbuf_free(packet);
    return ERR_OK;
}

void on_error(void *argument, err_t) {
    Connection *connection = static_cast<Connection *>(argument);
    if (connection && connection->updating) {
        updater_abort();
    }
    if (connection) {
        connection->pcb = nullptr;
        connection->used = false;
    }
}

err_t on_poll(void *argument, tcp_pcb *pcb) {
    Connection *connection = static_cast<Connection *>(argument);
    if (connection && ++connection->idle > idle_polls) {
        release(*connection);
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    return ERR_OK;
}

err_t on_accept(void *, tcp_pcb *pcb, err_t error) {
    if (error != ERR_OK || !pcb) {
        log_line("control: accept error %d", error);
        return ERR_VAL;
    }
    Connection *connection = nullptr;
    for (Connection &candidate : connections) {
        if (!candidate.used) {
            connection = &candidate;
            break;
        }
    }
    if (!connection) {
        log_line("control: no free connection slot");
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    connection->used = true;
    connection->waiting_for_scan = false;
    connection->closing = false;
    connection->updating = false;
    connection->update_remaining = 0;
    connection->idle = 0;
    connection->pcb = pcb;
    connection->request_length = 0;
    connection->response_length = 0;
    connection->response_sent = 0;
    tcp_arg(pcb, connection);
    tcp_recv(pcb, on_receive);
    tcp_sent(pcb, on_sent);
    tcp_err(pcb, on_error);
    tcp_poll(pcb, on_poll, 2);
    return ERR_OK;
}

}

void control_init() {
    tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    ip_addr_t address = brain_link_address(BRAINWAVE_DEBUG_FORWARD ? 0 : brain_link_router);
    if (tcp_bind(pcb, &address, control_port) != ERR_OK) {
        log_line("control: bind failed");
        return;
    }
    listener = tcp_listen_with_backlog(pcb, connection_limit);
    tcp_accept(listener, on_accept);
}

void control_task() {
    for (Connection &connection : connections) {
        if (connection.used && connection.waiting_for_scan && !wifi::scan_running() &&
            wifi::scan_generation() != connection.scan_generation) {
            connection.waiting_for_scan = false;
            write_scan(connection);
            finish(connection);
        }
    }
}
