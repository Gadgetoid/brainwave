// Firmware updates into the inactive A/B slot.
#pragma once

#include <cstddef>
#include <cstdint>

void updater_init();
void updater_task(uint32_t now_ms);
const char *updater_slot_name();
bool updater_begin(uint32_t size, const uint8_t *expected_hash, const char *&error);
bool updater_write(const uint8_t *data, size_t length, const char *&error);
bool updater_finish(const char *&error);
void updater_abort();
bool updater_active();
