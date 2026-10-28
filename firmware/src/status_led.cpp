// Drives the CYW43's LED from the link state, and never while joining a network, when an LED ioctl can block indefinitely.
#include "status_led.h"

#include "pico/cyw43_arch.h"

#include "rndis_host.h"
#include "wifi.h"

namespace {

bool shown = false;

bool wanted_state(uint32_t now_ms) {
    bool linked = rndis::is_up();
    bool online = wifi::connected();
    if (linked && online) {
        return true;
    }
    if (linked) {
        return (now_ms / 500) % 2;
    }
    if (online) {
        return (now_ms % 2000) < 100;
    }
    return (now_ms % 2000) < 30;
}

}

void status_led_update(uint32_t now_ms) {
    bool wanted = wanted_state(now_ms);
    if (wanted == shown || wifi::joining()) {
        return;
    }
    shown = wanted;
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, wanted);
}
