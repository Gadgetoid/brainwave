// Battery voltage from the LiPo 2XL's VBAT sense pin.
#pragma once

#include <cstdint>

void battery_init();
uint32_t battery_millivolts();
