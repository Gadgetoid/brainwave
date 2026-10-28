// The Advanced tab: power and debugging options, USB storage, module firmware and the module log.
#pragma once

#include <windows.h>

constexpr int advanced_storage_command = 401;
constexpr int advanced_quit_command = 402;
constexpr int advanced_firmware_command = 403;

HWND advanced_create(HWND parent);
void advanced_update();
void advanced_set_log(const char *text);
bool advanced_battery_wanted();
bool advanced_low_vbus_wanted();
void advanced_shown(bool shown);
