// Client for the module's control protocol: reply structures and the blocking request functions used by the worker thread.
#pragma once

#include <windows.h>

constexpr unsigned short module_port = 2350;
constexpr int module_network_limit = 32;
constexpr int module_ssid_hex_bytes = 72;
constexpr int module_ssid_characters = 40;

struct module_network {
    char ssid_hex[module_ssid_hex_bytes];
    wchar_t ssid[module_ssid_characters];
    int rssi;
    char security[8];
    int channel;
    bool saved;
};

struct module_status {
    bool reachable;
    char version[16];
    char wifi[16];
    char ssid_hex[module_ssid_hex_bytes];
    wchar_t ssid[module_ssid_characters];
    char security[8];
    char channel[8];
    char bssid[20];
    char ip[16];
    char netmask[16];
    char gateway[16];
    char dns[16];
    char rssi[8];
    char brain[16];
    char mac[20];
    char slot[16];
    int battery_millivolts;
    int uptime_seconds;
    bool has_uptime;
    bool debug_forward;
    int known;
    char failure[64];
};

struct module_known {
    bool ok;
    int count;
    char ssid_hex[module_network_limit][module_ssid_hex_bytes];
    wchar_t ssid[module_network_limit][module_ssid_characters];
};

struct module_scan {
    bool ok;
    int count;
    module_network networks[module_network_limit];
    char error[64];
};

unsigned long module_address();
bool module_query_status(unsigned long address, module_status &status);
bool module_query_scan(unsigned long address, module_scan &scan);
bool module_query_known(unsigned long address, module_known &known);
bool module_query_log(unsigned long address, char *text, int text_bytes);
bool module_send_command(unsigned long address, const char *command, char *error, int error_bytes);

typedef void (*module_update_progress)(DWORD sent, DWORD total, void *context);
bool module_update(unsigned long address, const BYTE *image, DWORD size, const char *digest_hex, module_update_progress progress, void *context,
                   char *error, int error_bytes);
