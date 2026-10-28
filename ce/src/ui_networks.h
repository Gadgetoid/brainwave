// The Networks tab: the network list and the actions on it.
#pragma once

#include <windows.h>

HWND networks_create(HWND parent);
void networks_update();
void networks_status_text(wchar_t *text, int characters);
void networks_start_scan();
void networks_shown(bool shown);
