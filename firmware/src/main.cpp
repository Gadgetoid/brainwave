// Start-up and the single-threaded main loop that polls USB, the RNDIS link, the CYW43, lwIP and the services.
#include "lwip/timeouts.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "tusb.h"

#include "battery.h"
#include "brain_link.h"
#include "control.h"
#include "crash.h"
#include "dhcp_server.h"
#include "dns_relay.h"
#include "log.h"
#include "napt_lwip.h"
#include "rndis_host.h"
#include "settings.h"
#include "status_led.h"
#include "updater.h"
#include "wifi.h"

namespace {

constexpr uint32_t housekeeping_ms = 1000;
constexpr uint32_t slow_step_ms = 500;
constexpr const char firmware_marker[] = "BRAINWAVE-FIRMWARE " BRAINWAVE_VERSION;

uint32_t now_ms() {
    return to_ms_since_boot(get_absolute_time());
}

template <typename Step> void timed_step(const char *name, Step step) {
    uint32_t started = now_ms();
    step();
    uint32_t taken = now_ms() - started;
    if (taken >= slow_step_ms) {
        log_line("slow: %s took %lu ms", name, static_cast<unsigned long>(taken));
    }
}

void start_usb_host() {
    tusb_rhport_init_t host = {};
    host.role = TUSB_ROLE_HOST;
    host.speed = TUSB_SPEED_AUTO;
    tusb_init(0, &host);
}

}

extern "C" void tuh_mount_cb(uint8_t device) {
    uint16_t vendor = 0;
    uint16_t product = 0;
    tuh_vid_pid_get(device, &vendor, &product);
    log_line("usb: device %04x:%04x attached", vendor, product);
}

extern "C" void tuh_umount_cb(uint8_t device) {
    log_line("usb: device detached");
}

int main() {
    log_restore();
    log_raw("---- boot\n");
    stdio_init_all();
    log_line("%s", firmware_marker);
    crash_report_boot();
    updater_init();
    settings::load();
    battery_init();

    if (cyw43_arch_init()) {
        log_line("cyw43: init failed");
        for (;;) {
            sleep_ms(1000);
        }
    }
    brain_link_init();
    napt_init();
    napt_set_debug_forward(BRAINWAVE_DEBUG_FORWARD);
    dhcp_server_init();
    dns_relay_init();
    control_init();
    wifi::init();
    start_usb_host();
    log_line("ready: link 10.88.0.1, control port 2350");

    crash_watchdog_start();
    uint32_t last_housekeeping = 0;
    for (;;) {
        crash_watchdog_feed();
        uint32_t now = now_ms();
        timed_step("usb", [] { tuh_task(); });
        timed_step("rndis", [now] { rndis::task(now); });
        timed_step("cyw43", [] { cyw43_arch_poll(); });
        timed_step("lwip", [] { sys_check_timeouts(); });
        timed_step("wifi", [now] { wifi::task(now); });
        timed_step("control", [] { control_task(); });
        timed_step("updater", [now] { updater_task(now); });
        if (now - last_housekeeping >= housekeeping_ms) {
            last_housekeeping = now;
            napt_expire();
            dns_relay_expire();
        }
        timed_step("led", [now] { status_led_update(now); });
    }
}
