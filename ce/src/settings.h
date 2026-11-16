// Small persistent app settings in the registry.
#pragma once

#include <windows.h>

DWORD settings_read(const wchar_t *name, DWORD fallback);
void settings_write(const wchar_t *name, DWORD value);
void settings_read_text(const wchar_t *name, wchar_t *text, int text_characters, const wchar_t *fallback);
void settings_write_text(const wchar_t *name, const wchar_t *text);
