// Loads bwpower.dll from the app's folder and wraps its IOCTLs for reading and switching the Brain's power source.
#include "power.h"

namespace {

constexpr const wchar_t *driver_key = L"Drivers\\Brainwave\\Power";
constexpr DWORD status_vbusvalid = 0x00000002;
constexpr DWORD dcdc_4p2_enabled = 0x00c00000;
constexpr DWORD five_volt_pwd_charge_4p2 = 0x00300000;
constexpr DWORD charge_pwd_battchrg = 0x00010000;
constexpr DWORD battery_monitor_value_shift = 16;
constexpr DWORD battery_monitor_value_mask = 0x3ff;
constexpr DWORD battery_monitor_millivolts_per_step = 8;

HANDLE device = INVALID_HANDLE_VALUE;

HANDLE open_device() {
    return CreateFileW(BRAINWAVE_POWER_DEVICE, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
}

bool register_driver(const wchar_t *dll_path) {
    HKEY key;
    DWORD disposition = 0;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, driver_key, 0, nullptr, 0, 0, nullptr, &key, &disposition) != ERROR_SUCCESS) {
        return false;
    }
    DWORD index = 1;
    LONG result = RegSetValueExW(key, L"Dll", 0, REG_SZ, reinterpret_cast<const BYTE *>(dll_path), (lstrlenW(dll_path) + 1) * sizeof(wchar_t));
    if (result == ERROR_SUCCESS) {
        result = RegSetValueExW(key, L"Prefix", 0, REG_SZ, reinterpret_cast<const BYTE *>(L"BWP"), 4 * sizeof(wchar_t));
    }
    if (result == ERROR_SUCCESS) {
        result = RegSetValueExW(key, L"Index", 0, REG_DWORD, reinterpret_cast<const BYTE *>(&index), sizeof(index));
    }
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

bool control(DWORD code, brainwave_power_state &state) {
    DWORD returned = 0;
    return device != INVALID_HANDLE_VALUE &&
           DeviceIoControl(device, code, nullptr, 0, &state, sizeof(state), &returned, nullptr) && returned == sizeof(state);
}

}

bool power_open(const wchar_t *program_directory, wchar_t *note, int note_characters) {
    if (device != INVALID_HANDLE_VALUE) {
        return true;
    }
    device = open_device();
    if (device != INVALID_HANDLE_VALUE) {
        lstrcpyW(note, L"power driver already loaded");
        return true;
    }
    wchar_t dll_path[MAX_PATH];
    wsprintfW(dll_path, L"%s\\bwpower.dll", program_directory);
    if (!register_driver(dll_path)) {
        wsprintfW(note, L"power driver: registry failed");
        return false;
    }
    HANDLE activated = ActivateDeviceEx(driver_key, nullptr, 0, nullptr);
    if (!activated) {
        wsprintfW(note, L"power driver: activate failed (%u)", static_cast<unsigned>(GetLastError()));
        return false;
    }
    device = open_device();
    if (device == INVALID_HANDLE_VALUE) {
        wsprintfW(note, L"power driver: open failed (%u)", static_cast<unsigned>(GetLastError()));
        return false;
    }
    lstrcpyW(note, L"power driver loaded");
    return true;
}

bool power_read(brainwave_power_state &state) {
    return control(BRAINWAVE_POWER_IOCTL_READ, state);
}

bool power_use_battery(brainwave_power_state &state) {
    return control(BRAINWAVE_POWER_IOCTL_USE_BATTERY, state);
}

bool power_set_low_vbus(bool enabled, brainwave_power_state &state, bool &unsupported) {
    unsupported = false;
    if (device == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD input = enabled ? 1 : 0;
    DWORD returned = 0;
    if (DeviceIoControl(device, BRAINWAVE_POWER_IOCTL_LOW_VBUS, &input, sizeof(input), &state, sizeof(state), &returned, nullptr) &&
        returned == sizeof(state)) {
        return true;
    }
    unsupported = GetLastError() == ERROR_NOT_SUPPORTED;
    return false;
}

void power_describe(const brainwave_power_state &state, wchar_t *text) {
    unsigned battery = ((state.battery_monitor >> battery_monitor_value_shift) & battery_monitor_value_mask) * battery_monitor_millivolts_per_step;
    bool from_usb = (state.dcdc_4p2 & dcdc_4p2_enabled) && (state.five_volt_control & five_volt_pwd_charge_4p2) != five_volt_pwd_charge_4p2;
    wsprintfW(text, L"VBUS %s, %s, charger %s, battery %u mV (5VCTRL %08X CHARGE %08X DCDC4P2 %08X STS %08X)",
              (state.status & status_vbusvalid) ? L"valid" : L"not valid", from_usb ? L"running from USB" : L"running from battery",
              (state.charge & charge_pwd_battchrg) ? L"off" : L"on", battery, static_cast<unsigned>(state.five_volt_control),
              static_cast<unsigned>(state.charge), static_cast<unsigned>(state.dcdc_4p2), static_cast<unsigned>(state.status));
}
