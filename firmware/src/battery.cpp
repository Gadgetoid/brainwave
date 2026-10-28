// Reads VBAT on GP43 through the ADC; the 1:3 divider is assumed, not yet checked against a meter.
#include "battery.h"

#include "hardware/adc.h"

namespace {

constexpr uint32_t sense_pin = PIMORONI_PICO_LIPO2XL_W_VBAT_SENSE_PIN;
constexpr uint32_t first_adc_pin = 40;
constexpr uint32_t divider = 3;
constexpr uint32_t reference_millivolts = 3300;
constexpr uint32_t samples = 16;

}

void battery_init() {
    adc_init();
    adc_gpio_init(sense_pin);
}

uint32_t battery_millivolts() {
    adc_select_input(sense_pin - first_adc_pin);
    uint32_t total = 0;
    for (uint32_t index = 0; index < samples; index++) {
        total += adc_read();
    }
    return total * reference_millivolts * divider / (samples * 4096);
}
