// Registers the RNDIS USB function client and NDIS keys in the RAM registry, and reads and switches the current USB function
// client.
#include "usb_client.h"

#include <string.h>

namespace {

constexpr DWORD device_search_by_guid = 3;
constexpr DWORD ioctl_ufn_get_current_client = 0x0022000c;
constexpr DWORD ioctl_ufn_change_current_client = 0x00220010;
constexpr DWORD ufn_client_name_bytes = 0x100;
constexpr DWORD ufn_client_info_bytes = 0x2f4;
constexpr const wchar_t *rndis_client_key = L"Drivers\\USB\\FunctionDrivers\\RNDIS";
constexpr const wchar_t *rndis_dll = L"rndisfn.dll";
constexpr DWORD rndis_vendor = 0x045e;
constexpr DWORD rndis_product = 0x0302;
constexpr DWORD rndis_sharing_product = 0x0303;

struct device_information {
    DWORD size;
    HANDLE device;
    HANDLE parent;
    wchar_t legacy_name[6];
    wchar_t device_key[MAX_PATH];
    wchar_t device_name[MAX_PATH];
    wchar_t bus_name[MAX_PATH];
};

struct interface_guid {
    DWORD data1;
    WORD data2;
    WORD data3;
    BYTE data4[8];
};

const interface_guid usb_function_bus = { 0xe2bdc372, 0x598f, 0x4619, { 0xbc, 0x50, 0x54, 0xb3, 0xf7, 0x84, 0x8d, 0x35 } };

}

extern "C" HANDLE FindFirstDevice(DWORD search_type, const void *search_parameter, device_information *information);
extern "C" BOOL FindClose(HANDLE search);

namespace {

void copy_text(wchar_t *destination, const wchar_t *source, DWORD characters) {
    DWORD index = 0;
    for (; index + 1 < characters && source[index]; index++) {
        destination[index] = source[index];
    }
    if (characters) {
        destination[index] = 0;
    }
}

LONG set_string(HKEY key, const wchar_t *name, const wchar_t *value) {
    return RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE *>(value), (lstrlenW(value) + 1) * sizeof(wchar_t));
}

LONG set_number(HKEY key, const wchar_t *name, DWORD value) {
    return RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE *>(&value), sizeof(value));
}

LONG set_multi_string(HKEY key, const wchar_t *name, const wchar_t *value) {
    wchar_t data[32];
    int length = lstrlenW(value);
    memset(data, 0, sizeof(data));
    memcpy(data, value, length * sizeof(wchar_t));
    return RegSetValueExW(key, name, 0, REG_MULTI_SZ, reinterpret_cast<const BYTE *>(data), (length + 2) * sizeof(wchar_t));
}

LONG create_key(const wchar_t *path, HKEY *key) {
    DWORD disposition;
    return RegCreateKeyExW(HKEY_LOCAL_MACHINE, path, 0, nullptr, 0, 0, nullptr, key, &disposition);
}

LONG register_ndis_driver(const wchar_t *path, bool no_device) {
    HKEY key;
    LONG result = create_key(path, &key);
    if (result != ERROR_SUCCESS) {
        return result;
    }
    result = set_string(key, L"ImagePath", rndis_dll);
    if (result == ERROR_SUCCESS) {
        result = set_string(key, L"Group", L"NDIS");
    }
    if (result == ERROR_SUCCESS && no_device) {
        result = set_number(key, L"NoDeviceCreate", 1);
    }
    RegCloseKey(key);
    return result;
}

HANDLE open_function_bus() {
    device_information *information = static_cast<device_information *>(LocalAlloc(LPTR, sizeof(device_information)));
    HANDLE bus = INVALID_HANDLE_VALUE;
    if (!information) {
        return INVALID_HANDLE_VALUE;
    }
    information->size = sizeof(device_information);
    HANDLE search = FindFirstDevice(device_search_by_guid, &usb_function_bus, information);
    if (search != INVALID_HANDLE_VALUE) {
        bus = CreateFileW(information->bus_name, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        FindClose(search);
    }
    LocalFree(information);
    return bus;
}

}

LONG usb_register_rndis_client() {
    HKEY key;
    LONG result = create_key(rndis_client_key, &key);
    if (result != ERROR_SUCCESS) {
        return result;
    }
    result = set_string(key, L"Dll", rndis_dll);
    if (result == ERROR_SUCCESS) {
        result = set_string(key, L"FriendlyName", L"Rndis");
    }
    if (result == ERROR_SUCCESS) {
        result = set_number(key, L"idVendor", rndis_vendor);
    }
    if (result == ERROR_SUCCESS) {
        result = set_number(key, L"idProduct", rndis_product);
    }
    if (result == ERROR_SUCCESS) {
        result = set_number(key, L"idProductInternetSharing", rndis_sharing_product);
    }
    if (result == ERROR_SUCCESS) {
        result = set_number(key, L"bcdDevice", 0);
    }
    if (result == ERROR_SUCCESS) {
        result = set_string(key, L"Manufacturer", L"pwsh2");
    }
    if (result == ERROR_SUCCESS) {
        result = set_string(key, L"Product", L"Brain RNDIS");
    }
    RegCloseKey(key);
    if (result == ERROR_SUCCESS) {
        result = register_ndis_driver(L"Comm\\RndisFn", true);
    }
    if (result == ERROR_SUCCESS) {
        result = create_key(L"Comm\\RndisFn\\Linkage", &key);
        if (result == ERROR_SUCCESS) {
            result = set_multi_string(key, L"Route", L"RndisFn1");
            RegCloseKey(key);
        }
    }
    if (result == ERROR_SUCCESS) {
        result = register_ndis_driver(L"Comm\\RndisFn1", false);
    }
    if (result == ERROR_SUCCESS) {
        result = create_key(L"Comm\\RndisFn1\\Parms", &key);
        if (result == ERROR_SUCCESS) {
            result = set_number(key, L"BusType", 0);
            if (result == ERROR_SUCCESS) {
                result = set_number(key, L"BusNumber", 0);
            }
            if (result == ERROR_SUCCESS) {
                result = set_number(key, L"RebindOnResume", 0);
            }
            RegCloseKey(key);
        }
    }
    if (result == ERROR_SUCCESS) {
        result = create_key(L"Comm\\RndisFn1\\Parms\\TcpIp", &key);
        if (result == ERROR_SUCCESS) {
            result = set_number(key, L"EnableDHCP", 1);
            RegCloseKey(key);
        }
    }
    return result;
}

bool usb_current_client(wchar_t *name, DWORD name_characters) {
    HANDLE bus = open_function_bus();
    if (bus == INVALID_HANDLE_VALUE) {
        return false;
    }
    BYTE *information = static_cast<BYTE *>(LocalAlloc(LPTR, ufn_client_info_bytes));
    DWORD returned = 0;
    bool ok = information &&
              DeviceIoControl(bus, ioctl_ufn_get_current_client, nullptr, 0, information, ufn_client_info_bytes, &returned, nullptr);
    if (ok) {
        copy_text(name, reinterpret_cast<const wchar_t *>(information), name_characters);
    }
    if (information) {
        LocalFree(information);
    }
    CloseHandle(bus);
    return ok;
}

bool usb_select_client(const wchar_t *name) {
    HANDLE bus = open_function_bus();
    if (bus == INVALID_HANDLE_VALUE) {
        return false;
    }
    wchar_t client[ufn_client_name_bytes / sizeof(wchar_t)];
    memset(client, 0, sizeof(client));
    copy_text(client, name, ufn_client_name_bytes / sizeof(wchar_t));
    bool ok = DeviceIoControl(bus, ioctl_ufn_change_current_client, client, sizeof(client), nullptr, 0, nullptr, nullptr);
    CloseHandle(bus);
    return ok;
}
