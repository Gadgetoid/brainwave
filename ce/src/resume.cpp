// Listens for CE power notifications and restarts the RNDIS USB client after a resume, which otherwise does not come back.
#include "resume.h"

#include <windows.h>

#include "app.h"
#include "usb_client.h"

namespace {

struct message_queue_options {
    DWORD size;
    DWORD flags;
    DWORD maximum_messages;
    DWORD maximum_message_bytes;
    BOOL read_access;
};

struct power_broadcast {
    DWORD message;
    DWORD flags;
    DWORD length;
    WCHAR system_power_state[1];
};

}

extern "C" {
HANDLE WINAPI CreateMsgQueue(LPCWSTR name, message_queue_options *options);
BOOL WINAPI ReadMsgQueue(HANDLE queue, LPVOID buffer, DWORD buffer_bytes, LPDWORD read_bytes, DWORD timeout, DWORD *flags);
HANDLE WINAPI RequestPowerNotifications(HANDLE queue, DWORD flags);
}

namespace {

constexpr DWORD power_broadcast_resume = 0x00000002;
constexpr DWORD power_notify_all = 0xffffffff;
constexpr DWORD message_bytes = sizeof(power_broadcast) + MAX_PATH * sizeof(WCHAR);
constexpr DWORD settle_milliseconds = 1500;
constexpr DWORD storage_milliseconds = 1000;

void restart_rndis_client() {
    wchar_t current[64] = L"";
    bool known = usb_current_client(current, 64);
    wchar_t line[128];
    if (!known || lstrcmpiW(current, usb_rndis_client)) {
        wsprintfW(line, L"resume: USB client is %s, leaving it", known ? current : L"unknown");
        append_log(line);
        return;
    }
    Sleep(settle_milliseconds);
    bool storage = usb_select_client(usb_mass_storage_client);
    Sleep(storage_milliseconds);
    bool rndis = usb_select_client(usb_rndis_client);
    wsprintfW(line, L"resume: restarted RNDIS client (storage %d, rndis %d, error %u)", storage ? 1 : 0, rndis ? 1 : 0,
              static_cast<unsigned>(rndis ? 0 : GetLastError()));
    append_log(line);
}

DWORD WINAPI resume_thread(LPVOID queue) {
    BYTE buffer[message_bytes];
    for (;;) {
        DWORD read = 0;
        DWORD flags = 0;
        if (!ReadMsgQueue(static_cast<HANDLE>(queue), buffer, sizeof(buffer), &read, INFINITE, &flags)) {
            append_log(L"resume: power notification queue closed");
            return 0;
        }
        const power_broadcast *broadcast = reinterpret_cast<const power_broadcast *>(buffer);
        if (read >= sizeof(DWORD) && broadcast->message == power_broadcast_resume) {
            append_log(L"resume: Brain resumed");
            restart_rndis_client();
        }
    }
}

}

void resume_watch_start() {
    message_queue_options options = {};
    options.size = sizeof(options);
    options.maximum_messages = 4;
    options.maximum_message_bytes = message_bytes;
    options.read_access = TRUE;
    HANDLE queue = CreateMsgQueue(nullptr, &options);
    if (!queue || !RequestPowerNotifications(queue, power_notify_all)) {
        wchar_t line[64];
        wsprintfW(line, L"resume: power notifications unavailable (%u)", static_cast<unsigned>(GetLastError()));
        append_log(line);
        return;
    }
    HANDLE thread = CreateThread(nullptr, 0, resume_thread, queue, 0, nullptr);
    if (thread) {
        CloseHandle(thread);
    }
}
