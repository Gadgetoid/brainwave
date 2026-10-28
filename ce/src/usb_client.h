// The Brain's USB function clients: register RNDIS, read and select the current client.
#pragma once

#include <windows.h>

constexpr const wchar_t *usb_rndis_client = L"RNDIS";
constexpr const wchar_t *usb_mass_storage_client = L"Mass_Storage_Class";

LONG usb_register_rndis_client();
bool usb_current_client(wchar_t *name, DWORD name_characters);
bool usb_select_client(const wchar_t *name);
