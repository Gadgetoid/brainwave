// The notification area icon showing WiFi signal.
#pragma once

#include <windows.h>

void tray_update(HWND window, bool reachable, bool connected, int bars, const wchar_t *tip);
void tray_remove(HWND window);
