// The status light: solid with the Brain and WiFi up, slow blink with the Brain only, short blips otherwise.
#pragma once

#include <cstdint>

void status_led_update(uint32_t now_ms);
