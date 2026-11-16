#pragma once

#include <windows.h>

constexpr int clock_server_characters = 64;

struct clock_report {
    bool ok;
    SYSTEMTIME old_time;
    SYSTEMTIME new_time;
    LONGLONG drift_milliseconds;
    DWORD round_trip_milliseconds;
    int stratum;
    char error[64];
};

void clock_sync_load();
bool clock_sync_enabled();
const wchar_t *clock_sync_server();
void clock_sync_set_enabled(bool enabled);
void clock_sync_set_server(const wchar_t *server);
bool clock_sync_can_start();
void clock_sync_now();
void clock_sync_watch();
void clock_sync_report(const clock_report &report);
