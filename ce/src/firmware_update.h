// Module firmware update: the brainwave.bin version probe and the background transfer to the module.
#pragma once

#include <windows.h>

constexpr int firmware_version_characters = 24;

enum class update_phase { idle, checking, sending, writing, restarting, finished, failed };

struct firmware_file_info {
    bool present;
    DWORD size;
    wchar_t version[firmware_version_characters];
};

struct update_report {
    update_phase phase;
    int percent;
    char error[64];
};

const firmware_file_info &firmware_probe();
bool firmware_update_start(unsigned long address);
bool firmware_update_running();
