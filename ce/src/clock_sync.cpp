#include "clock_sync.h"

#include <string.h>

#include "app.h"
#include "model.h"
#include "settings.h"
#include "sntp.h"
#include "text.h"

namespace {

constexpr const wchar_t *enabled_setting = L"SetClock";
constexpr const wchar_t *server_setting = L"TimeServer";
constexpr const wchar_t *default_server = L"pool.ntp.org";
constexpr DWORD resync_interval_milliseconds = 3 * 60 * 60 * 1000;
constexpr DWORD retry_interval_milliseconds = 5 * 60 * 1000;
constexpr LONGLONG filetime_ticks_per_millisecond = 10000;
constexpr DWORD seconds_per_minute = 60;
constexpr DWORD seconds_per_hour = 60 * 60;
constexpr DWORD seconds_per_day = 24 * 60 * 60;

bool enabled = true;
wchar_t server[clock_server_characters] = L"";
wchar_t running_server[clock_server_characters] = L"";
char thread_server[clock_server_characters] = "";
volatile LONG running = 0;
bool attempted = false;
bool last_ok = false;
DWORD last_attempt_tick = 0;
bool was_connected = false;

ULONGLONG file_time_value(const FILETIME &file) {
    return static_cast<ULONGLONG>(file.dwHighDateTime) << 32 | file.dwLowDateTime;
}

FILETIME file_time_from(ULONGLONG value) {
    FILETIME file;
    file.dwLowDateTime = static_cast<DWORD>(value);
    file.dwHighDateTime = static_cast<DWORD>(value >> 32);
    return file;
}

DWORD WINAPI sync_thread(LPVOID) {
    clock_report *report = static_cast<clock_report *>(LocalAlloc(LPTR, sizeof(clock_report)));
    if (report) {
        sntp_result result = {};
        if (sntp_query(thread_server, result, report->error, sizeof(report->error))) {
            report->stratum = result.stratum;
            report->round_trip_milliseconds = result.round_trip_milliseconds;
            FILETIME old_file;
            GetSystemTime(&report->old_time);
            SystemTimeToFileTime(&report->old_time, &old_file);
            FILETIME new_file = file_time_from(result.utc_time);
            FileTimeToSystemTime(&new_file, &report->new_time);
            if (SetSystemTime(&report->new_time)) {
                report->ok = true;
                report->drift_milliseconds =
                    (static_cast<LONGLONG>(result.utc_time) - static_cast<LONGLONG>(file_time_value(old_file))) / filetime_ticks_per_millisecond;
            } else {
                strcpy(report->error, "could not set the clock");
            }
        }
        if (!PostMessageW(main_window, clock_message, 0, reinterpret_cast<LPARAM>(report))) {
            LocalFree(report);
        }
    }
    InterlockedExchange(&running, 0);
    return 0;
}

void start() {
    if (InterlockedCompareExchange(&running, 1, 0) != 0) {
        return;
    }
    lstrcpynW(running_server, server, clock_server_characters);
    wide_to_ascii(server, thread_server, clock_server_characters);
    attempted = true;
    last_ok = false;
    last_attempt_tick = GetTickCount();
    HANDLE thread = CreateThread(nullptr, 0, sync_thread, nullptr, 0, nullptr);
    if (!thread) {
        InterlockedExchange(&running, 0);
        lstrcpyW(model.clock_note, L"Not set: could not start a thread");
        append_log(L"clock: could not start a thread");
        return;
    }
    CloseHandle(thread);
    wsprintfW(model.clock_note, L"Asking %s...", running_server);
}

void describe_drift(LONGLONG drift_milliseconds, wchar_t *text) {
    ULONGLONG magnitude = static_cast<ULONGLONG>(drift_milliseconds < 0 ? -drift_milliseconds : drift_milliseconds);
    const wchar_t *direction = drift_milliseconds > 0 ? L"slow" : L"fast";
    DWORD seconds = static_cast<DWORD>(magnitude / 1000);
    if (seconds < 1) {
        lstrcpyW(text, L"It was right to the second.");
    } else if (seconds < 2 * seconds_per_minute) {
        wsprintfW(text, L"It was %u s %s.", seconds, direction);
    } else if (seconds < 2 * seconds_per_hour) {
        wsprintfW(text, L"It was %u min %s.", seconds / seconds_per_minute, direction);
    } else if (seconds < 2 * seconds_per_day) {
        wsprintfW(text, L"It was %u h %s.", seconds / seconds_per_hour, direction);
    } else {
        wsprintfW(text, L"It was %u days %s.", seconds / seconds_per_day, direction);
    }
}

void format_time(const SYSTEMTIME &time, wchar_t *text) {
    wsprintfW(text, L"%04u-%02u-%02u %02u:%02u:%02u", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
}

void set_idle_note() {
    lstrcpyW(model.clock_note, enabled ? L"Not set yet." : L"");
}

}

void clock_sync_load() {
    enabled = settings_read(enabled_setting, 1) != 0;
    settings_read_text(server_setting, server, clock_server_characters, default_server);
    set_idle_note();
}

bool clock_sync_enabled() {
    return enabled;
}

const wchar_t *clock_sync_server() {
    return server;
}

void clock_sync_set_enabled(bool wanted) {
    enabled = wanted;
    settings_write(enabled_setting, enabled ? 1 : 0);
    append_log(enabled ? L"set clock: on" : L"set clock: off");
    attempted = false;
    if (!running) {
        set_idle_note();
    }
    clock_sync_watch();
    app_refresh_views();
}

void clock_sync_set_server(const wchar_t *wanted) {
    if (!lstrcmpW(server, wanted)) {
        return;
    }
    lstrcpynW(server, wanted, clock_server_characters);
    settings_write_text(server_setting, server);
}

bool clock_sync_can_start() {
    return model_connected() && !running && server[0];
}

void clock_sync_now() {
    if (clock_sync_can_start()) {
        start();
        app_refresh_views();
    }
}

void clock_sync_watch() {
    bool connected = model_connected();
    if (connected && !was_connected && !last_ok) {
        attempted = false;
    }
    was_connected = connected;
    if (!enabled || !connected || running || !server[0]) {
        return;
    }
    if (attempted && GetTickCount() - last_attempt_tick < (last_ok ? resync_interval_milliseconds : retry_interval_milliseconds)) {
        return;
    }
    start();
}

void clock_sync_report(const clock_report &report) {
    last_ok = report.ok;
    wchar_t line[256];
    if (report.ok) {
        SYSTEMTIME local;
        GetLocalTime(&local);
        wchar_t drift[48];
        describe_drift(report.drift_milliseconds, drift);
        wsprintfW(model.clock_note, L"Set at %02u:%02u. %s", local.wHour, local.wMinute, drift);
        wchar_t old_text[24];
        wchar_t new_text[24];
        format_time(report.old_time, old_text);
        format_time(report.new_time, new_text);
        LONGLONG drift_milliseconds = report.drift_milliseconds;
        ULONGLONG magnitude = static_cast<ULONGLONG>(drift_milliseconds < 0 ? -drift_milliseconds : drift_milliseconds);
        wsprintfW(line, L"clock: set from %s, stratum %d, %u ms round trip, was %s UTC, now %s UTC, drift %s%u.%03u s", running_server,
                  report.stratum, static_cast<unsigned>(report.round_trip_milliseconds), old_text, new_text, drift_milliseconds < 0 ? L"-" : L"+",
                  static_cast<unsigned>(magnitude / 1000), static_cast<unsigned>(magnitude % 1000));
        PostMessageW(HWND_BROADCAST, WM_TIMECHANGE, 0, 0);
    } else {
        wchar_t error[64];
        ascii_to_wide(report.error, error, 64);
        wsprintfW(model.clock_note, L"Not set: %s", error);
        wsprintfW(line, L"clock: %s failed: %s", running_server, error);
    }
    append_log(line);
    app_refresh_views();
}
