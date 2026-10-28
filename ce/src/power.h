// Access to the bwpower.dll power driver: read the power registers, run the Brain from its battery, lower the VBUS threshold.
#pragma once

#include "../driver/brainwave_power.h"

bool power_open(const wchar_t *program_directory, wchar_t *note, int note_characters);
bool power_read(brainwave_power_state &state);
bool power_use_battery(brainwave_power_state &state);
bool power_set_low_vbus(bool enabled, brainwave_power_state &state, bool &unsupported);
void power_describe(const brainwave_power_state &state, wchar_t *text);
