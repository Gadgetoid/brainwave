// Puts the Brain's USB port into RNDIS networking for the module: copies rndisfn.dll into \Windows if needed, selects the RNDIS
// client, and moves a running Debug Agent onto the network so it does not take the port back. Also switches back to mass storage.
#include "usb_activation.h"

#include <windows.h>

#include "app.h"
#include "model.h"
#include "usb_client.h"

namespace {

constexpr const wchar_t *windows_driver_path = L"\\Windows\\rndisfn.dll";
constexpr const wchar_t *debug_agent_window_class = L"BrainDebugAgentStatus";
constexpr const wchar_t *debug_agent_path = L"\\Storage Card\\brainshell\\usb\\debugmgr.exe";
constexpr DWORD debug_agent_wait_milliseconds = 5000;

bool stop_debug_agent() {
    HWND agent = FindWindowW(debug_agent_window_class, nullptr);
    if (!agent) {
        return false;
    }
    PostMessageW(agent, WM_CLOSE, 0, 0);
    DWORD waited = 0;
    while (FindWindowW(debug_agent_window_class, nullptr) && waited < debug_agent_wait_milliseconds) {
        Sleep(100);
        waited += 100;
    }
    wchar_t message[64];
    wsprintfW(message, L"stopped Debug Agent: %u ms", static_cast<unsigned>(waited));
    append_log(message);
    return true;
}

void restart_debug_agent() {
    PROCESS_INFORMATION process = {};
    wchar_t arguments[] = L"auto";
    BOOL started = CreateProcessW(debug_agent_path, arguments, nullptr, nullptr, FALSE, 0, nullptr, nullptr, nullptr, &process);
    if (started) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    append_log(started ? L"restarted Debug Agent on the network" : L"could not restart Debug Agent");
}

}

void usb_install_rndis_driver() {
    if (GetFileAttributesW(windows_driver_path) != 0xFFFFFFFF) {
        return;
    }
    wchar_t source[MAX_PATH];
    wchar_t message[96];
    wsprintfW(source, L"%s\\rndisfn.dll", program_directory);
    BOOL copied = CopyFileW(source, windows_driver_path, TRUE);
    wsprintfW(message, L"copy rndisfn.dll to \\Windows: %d error=%u", copied ? 1 : 0, copied ? 0u : static_cast<unsigned>(GetLastError()));
    append_log(message);
}

void usb_activate_rndis() {
    wchar_t message[128];
    LONG registered = usb_register_rndis_client();
    wchar_t current[64] = L"";
    bool known = usb_current_client(current, 64);
    bool already = known && !lstrcmpiW(current, usb_rndis_client);
    bool agent_was_running = !already && stop_debug_agent();
    bool selected = already || (registered == ERROR_SUCCESS && usb_select_client(usb_rndis_client));
    if (agent_was_running && selected) {
        restart_debug_agent();
    }
    DWORD error = selected ? 0 : GetLastError();
    wsprintfW(message, L"activate rndis: register=%d current=%s already=%d select=%d error=%u", static_cast<int>(registered),
              known ? current : L"?", already ? 1 : 0, selected ? 1 : 0, static_cast<unsigned>(error));
    append_log(message);
    model.usb = already ? usb_state::ready : (selected ? usb_state::replug_needed : usb_state::failed);
}

bool usb_restore_storage() {
    bool selected = usb_select_client(usb_mass_storage_client);
    wchar_t message[64];
    wsprintfW(message, L"select mass storage: %d error=%u", selected ? 1 : 0, selected ? 0u : static_cast<unsigned>(GetLastError()));
    append_log(message);
    return selected;
}
