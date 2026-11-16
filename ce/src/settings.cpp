// Small persistent app settings (last tab, power options) in HKCU\Software\Brainwave.
#include "settings.h"

namespace {

constexpr const wchar_t *settings_key = L"Software\\Brainwave";

}

DWORD settings_read(const wchar_t *name, DWORD fallback) {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, settings_key, 0, 0, &key) != ERROR_SUCCESS) {
        return fallback;
    }
    DWORD value = fallback;
    DWORD size = sizeof(value);
    DWORD type = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE *>(&value), &size) != ERROR_SUCCESS || type != REG_DWORD) {
        value = fallback;
    }
    RegCloseKey(key);
    return value;
}

void settings_write(const wchar_t *name, DWORD value) {
    HKEY key;
    DWORD disposition = 0;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, settings_key, 0, nullptr, 0, 0, nullptr, &key, &disposition) != ERROR_SUCCESS) {
        return;
    }
    RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE *>(&value), sizeof(value));
    RegCloseKey(key);
}

void settings_read_text(const wchar_t *name, wchar_t *text, int text_characters, const wchar_t *fallback) {
    lstrcpynW(text, fallback, text_characters);
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, settings_key, 0, 0, &key) != ERROR_SUCCESS) {
        return;
    }
    DWORD size = static_cast<DWORD>((text_characters - 1) * sizeof(wchar_t));
    DWORD type = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE *>(text), &size) != ERROR_SUCCESS || type != REG_SZ) {
        lstrcpynW(text, fallback, text_characters);
    } else {
        text[size / sizeof(wchar_t)] = 0;
    }
    RegCloseKey(key);
}

void settings_write_text(const wchar_t *name, const wchar_t *text) {
    HKEY key;
    DWORD disposition = 0;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, settings_key, 0, nullptr, 0, 0, nullptr, &key, &disposition) != ERROR_SUCCESS) {
        return;
    }
    RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE *>(text), static_cast<DWORD>((lstrlenW(text) + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
}
