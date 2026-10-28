// The modal network password dialog.
#pragma once

#include <windows.h>

bool password_dialog(HWND owner, const wchar_t *network, bool wep, wchar_t *password, int password_characters);
