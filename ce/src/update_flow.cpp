// Drives a module firmware update from the Advanced tab: asks for confirmation, turns the worker's progress reports into the
// status line, and watches the module's status until it is running from the other slot.
#include "update_flow.h"

#include <windows.h>
#include <string.h>

#include "app.h"
#include "model.h"
#include "text.h"

namespace {

constexpr DWORD restart_timeout_milliseconds = 90000;

}

void update_flow_start() {
    if (firmware_update_running() || !model.status.reachable) {
        return;
    }
    const firmware_file_info &file = firmware_probe();
    if (!file.present) {
        return;
    }
    wchar_t running_version[16];
    ascii_to_wide(model.status.version[0] ? model.status.version : "unknown", running_version, 16);
    wchar_t question[256];
    wsprintfW(question, L"Update Brainwave from %s to %s? The module restarts when the update is written, and goes back to %s by itself if the new firmware does not start.",
              running_version, file.version, running_version);
    if (MessageBoxW(main_window, question, L"Update Module Firmware", MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
    }
    int copied = 0;
    for (; copied + 1 < static_cast<int>(sizeof(model.update_from_slot)) && model.status.slot[copied]; copied++) {
        model.update_from_slot[copied] = model.status.slot[copied];
    }
    model.update_from_slot[copied] = 0;
    model.update_percent = 0;
    if (!firmware_update_start(module_address())) {
        return;
    }
    model.update = update_phase::checking;
    lstrcpyW(model.update_note, L"Checking brainwave.bin...");
    wchar_t line[96];
    wsprintfW(line, L"firmware update: %s to %s", running_version, file.version);
    append_log(line);
    app_refresh_views();
}

void update_flow_report(const update_report &report) {
    model.update = report.phase;
    model.update_percent = report.percent;
    switch (report.phase) {
    case update_phase::checking:
        lstrcpyW(model.update_note, L"Checking brainwave.bin...");
        break;
    case update_phase::sending:
        wsprintfW(model.update_note, L"Sending firmware: %d%%", report.percent);
        break;
    case update_phase::writing:
        lstrcpyW(model.update_note, L"Writing and verifying...");
        break;
    case update_phase::restarting:
        lstrcpyW(model.update_note, L"Module restarting...");
        model.update_tick = GetTickCount();
        append_log(L"firmware update: written, module restarting");
        break;
    case update_phase::failed: {
        wchar_t error[64];
        ascii_to_wide(report.error, error, 64);
        wsprintfW(model.update_note, L"Update failed: %s", error);
        wchar_t line[96];
        wsprintfW(line, L"firmware update failed: %s", error);
        append_log(line);
        break;
    }
    default:
        break;
    }
    app_refresh_views();
}

void update_flow_watch_restart() {
    if (model.update != update_phase::restarting) {
        return;
    }
    if (model.status.reachable && model.status.slot[0] && strcmp(model.status.slot, model.update_from_slot)) {
        wchar_t version[16];
        wchar_t slot[16];
        ascii_to_wide(model.status.version, version, 16);
        ascii_to_wide(model.status.slot, slot, 16);
        wsprintfW(model.update_note, L"Updated: running %s from slot %s.", version, slot);
        model.update = update_phase::finished;
        wchar_t line[96];
        wsprintfW(line, L"firmware update: running %s from slot %s", version, slot);
        append_log(line);
    } else if (GetTickCount() - model.update_tick > restart_timeout_milliseconds) {
        lstrcpyW(model.update_note, L"The module did not come back in the new slot. It may have gone back to the old firmware.");
        model.update = update_phase::failed;
        append_log(L"firmware update: no restart into the other slot within 90 s");
    }
}
