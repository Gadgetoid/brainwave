// Decides how the Brain is powered while the module is attached: keeps it on its own battery and the low VBUS threshold in place
// when the Advanced options ask for them, re-applies them if Sharp's power driver undoes them, and runs the power-* test modes.
#include "power_policy.h"

#include <windows.h>

#include "app.h"
#include "model.h"
#include "power.h"
#include "ui_advanced.h"

namespace {

constexpr DWORD status_vbusvalid = 0x00000002;
constexpr DWORD dcdc_4p2_enabled = 0x00c00000;
constexpr DWORD five_volt_pwd_charge_4p2 = 0x00300000;
constexpr DWORD charge_pwd_battchrg = 0x00010000;

bool power_tried = false;
bool power_applied_once = false;
bool low_vbus_applied_once = false;
bool low_vbus_unsupported = false;
wchar_t power_note[64] = L"";

void log_power(const wchar_t *label, const brainwave_power_state &state) {
    wchar_t description[200];
    wchar_t line[240];
    power_describe(state, description);
    wsprintfW(line, L"%s: %s changed=%u", label, description, static_cast<unsigned>(state.changed));
    append_log(line);
}

unsigned battery_millivolts(const brainwave_power_state &state) {
    return ((state.battery_monitor >> 16) & 0x3ff) * 8;
}

void describe_power(const brainwave_power_state &state) {
    bool from_usb = (state.dcdc_4p2 & dcdc_4p2_enabled) && (state.five_volt_control & five_volt_pwd_charge_4p2) != five_volt_pwd_charge_4p2;
    bool charging = !(state.charge & charge_pwd_battchrg);
    unsigned millivolts = battery_millivolts(state);
    const wchar_t *source = from_usb ? L"Drawing power from USB" : L"Running from its own battery";
    const wchar_t *charger = charging ? L"charging" : L"not charging";
    const wchar_t *vbus = (state.status & status_vbusvalid) ? L"" : L" USB power is below the Brain's 5 V threshold.";
    wsprintfW(model.power_description, L"%s, %s. Battery %u.%02u V.%s", source, charger, millivolts / 1000, (millivolts % 1000) / 10, vbus);
}

void apply_low_vbus(bool wanted, brainwave_power_state &state) {
    if (!wanted && !low_vbus_applied_once) {
        return;
    }
    bool unsupported = false;
    if (power_set_low_vbus(wanted, state, unsupported)) {
        low_vbus_unsupported = false;
        if (state.changed & BRAINWAVE_POWER_CHANGED_VBUS_THRESHOLD) {
            if (!wanted) {
                append_log(L"low vbus: restored Sharp's threshold");
                low_vbus_applied_once = false;
            } else {
                log_power(low_vbus_applied_once ? L"vbus threshold reverted, reapplied" : L"low vbus threshold", state);
                low_vbus_applied_once = true;
            }
        } else if (wanted) {
            low_vbus_applied_once = true;
        }
    } else if (unsupported && !low_vbus_unsupported) {
        low_vbus_unsupported = true;
        append_log(L"low vbus: not supported by the loaded power driver; restart the Brain to enable");
    }
}

}

bool power_policy_run_test(const wchar_t *command_line) {
    wchar_t note[64] = L"";
    bool opened = power_open(program_directory, note, 64);
    append_log(note);
    if (!opened) {
        return command_line && !lstrcmpiW(command_line, L"power-battery");
    }
    brainwave_power_state state = {};
    if (power_read(state)) {
        log_power(L"power", state);
    }
    if (!command_line || lstrcmpiW(command_line, L"power-battery")) {
        return false;
    }
    if (power_use_battery(state)) {
        log_power(L"use battery", state);
    }
    Sleep(2000);
    if (power_read(state)) {
        log_power(L"after 2 s", state);
    }
    Sleep(10000);
    if (power_read(state)) {
        log_power(L"after 12 s", state);
    }
    return true;
}

void power_policy_update() {
    if (!power_tried) {
        power_tried = true;
        model.power_available = power_open(program_directory, power_note, 64);
        append_log(power_note);
    }
    if (!model.power_available) {
        wsprintfW(model.power_description, L"Power control is unavailable (%s).", power_note);
        return;
    }
    brainwave_power_state state = {};
    bool low_vbus_wanted = advanced_low_vbus_wanted();
    apply_low_vbus(low_vbus_wanted, state);
    bool apply = advanced_battery_wanted() && model.status.reachable;
    bool ok = apply ? power_use_battery(state) : power_read(state);
    if (!ok) {
        lstrcpyW(model.power_description, L"Could not read the Brain's power state.");
        return;
    }
    if (apply && state.changed) {
        log_power(power_applied_once ? L"power source reverted, reapplied" : L"brain on battery", state);
        power_applied_once = true;
    }
    describe_power(state);
    if (low_vbus_wanted && low_vbus_unsupported) {
        int length = lstrlenW(model.power_description);
        lstrcpynW(model.power_description + length, L" Restart the Brain to enable low-voltage USB.", 160 - length);
    }
}
