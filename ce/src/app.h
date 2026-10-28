// Shared declarations for Brainwave WiFi: window messages from the worker thread, application globals, logging and the worker
// requests.
#pragma once

#include <windows.h>

#include "module.h"

constexpr UINT status_message = WM_APP + 1;
constexpr UINT scan_message = WM_APP + 2;
constexpr UINT command_message = WM_APP + 3;
constexpr UINT known_message = WM_APP + 4;
constexpr UINT log_message = WM_APP + 5;
constexpr UINT tray_message = WM_APP + 6;
constexpr UINT show_message = WM_APP + 7;
constexpr UINT update_message = WM_APP + 8;

constexpr int command_bytes = 256;
constexpr int module_log_bytes = 6144;

struct command_result {
    bool ok;
    char command[command_bytes];
    char error[64];
};

struct module_log_text {
    bool ok;
    char text[module_log_bytes];
};

extern wchar_t program_directory[MAX_PATH];
extern HINSTANCE program_instance;
extern HWND main_window;

void app_find_program_directory();
void app_refresh_views();
void append_log(const wchar_t *text);

void worker_start();
void request_scan();
void request_known();
void request_log();
void request_command(const char *command);
void request_status_now();

void build_command(char *command, const char *verb, const char *first, const char *second);
