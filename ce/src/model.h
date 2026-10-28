// The application model: everything the views show, filled in by the worker thread's results.
#pragma once

#include <windows.h>

#include "firmware_update.h"
#include "module.h"

constexpr int history_limit = 150;
constexpr int history_gap = -1000;

enum class usb_state { ready, replug_needed, failed };

struct app_model {
    module_status status;
    bool status_seen;
    module_scan scan;
    bool scan_seen;
    bool scanning;
    module_known known;
    wchar_t pending_ssid[module_ssid_characters];
    char pending_ssid_hex[module_ssid_hex_bytes];
    DWORD pending_tick;
    wchar_t action_note[96];
    usb_state usb;
    int history[history_limit];
    int history_count;
    int history_start;
    wchar_t power_description[160];
    bool power_available;
    update_phase update;
    int update_percent;
    wchar_t update_note[128];
    char update_from_slot[16];
    DWORD update_tick;
};

extern app_model model;

void model_add_history(int rssi);
int model_history_at(int index);
bool model_connected();
int model_rssi();
