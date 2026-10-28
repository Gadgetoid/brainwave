// Small persistent app settings in the registry.
#pragma once

#include <windows.h>

DWORD settings_read(const wchar_t *name, DWORD fallback);
void settings_write(const wchar_t *name, DWORD value);
