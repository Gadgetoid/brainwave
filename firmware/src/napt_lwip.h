// Connects the address translator to lwIP.
#pragma once

#include <cstddef>
#include <cstdint>

void napt_init();
void napt_set_outside_address(uint32_t host_order);
void napt_expire();
size_t napt_active_mappings();
void napt_set_debug_forward(bool enabled);
bool napt_debug_forward();
