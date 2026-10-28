// The worker thread: polls the module's status every 2 s and runs queued requests, posting results to the main window.
#include "app.h"

#include <string.h>

namespace {

constexpr DWORD status_interval_milliseconds = 2000;
constexpr int command_queue_limit = 4;

CRITICAL_SECTION request_lock;
HANDLE request_event = nullptr;
bool scan_wanted = false;
bool known_wanted = false;
bool log_wanted = false;
char command_queue[command_queue_limit][command_bytes];
int command_queue_count = 0;

void post_or_free(UINT message, void *data) {
    if (!PostMessageW(main_window, message, 0, reinterpret_cast<LPARAM>(data))) {
        LocalFree(data);
    }
}

void copy_text(char *destination, const char *source, int destination_bytes) {
    int index = 0;
    for (; index + 1 < destination_bytes && source[index]; index++) {
        destination[index] = source[index];
    }
    destination[index] = 0;
}

template <typename Result> Result *allocate() {
    return static_cast<Result *>(LocalAlloc(LPTR, sizeof(Result)));
}

bool take_command(char *command) {
    bool taken = false;
    EnterCriticalSection(&request_lock);
    if (command_queue_count) {
        memcpy(command, command_queue[0], command_bytes);
        memmove(command_queue[0], command_queue[1], (command_queue_count - 1) * command_bytes);
        command_queue_count--;
        taken = true;
    }
    LeaveCriticalSection(&request_lock);
    return taken;
}

bool take_flag(bool &flag) {
    EnterCriticalSection(&request_lock);
    bool wanted = flag;
    flag = false;
    LeaveCriticalSection(&request_lock);
    return wanted;
}

void run_command(unsigned long address, const char *command) {
    command_result *result = allocate<command_result>();
    if (!result) {
        return;
    }
    copy_text(result->command, command, command_bytes);
    result->ok = module_send_command(address, command, result->error, sizeof(result->error));
    post_or_free(command_message, result);
}

DWORD WINAPI worker_thread(LPVOID) {
    char command[command_bytes];
    for (;;) {
        WaitForSingleObject(request_event, status_interval_milliseconds);
        unsigned long address = module_address();
        while (take_command(command)) {
            run_command(address, command);
        }
        if (take_flag(scan_wanted)) {
            module_scan *scan = allocate<module_scan>();
            if (scan) {
                module_query_scan(address, *scan);
                post_or_free(scan_message, scan);
            }
            EnterCriticalSection(&request_lock);
            known_wanted = true;
            LeaveCriticalSection(&request_lock);
        }
        if (take_flag(known_wanted)) {
            module_known *known = allocate<module_known>();
            if (known) {
                module_query_known(address, *known);
                post_or_free(known_message, known);
            }
        }
        if (take_flag(log_wanted)) {
            module_log_text *log = allocate<module_log_text>();
            if (log) {
                log->ok = module_query_log(address, log->text, sizeof(log->text));
                post_or_free(log_message, log);
            }
        }
        module_status *status = allocate<module_status>();
        if (status) {
            module_query_status(address, *status);
            post_or_free(status_message, status);
        }
    }
}

void set_flag(bool &flag) {
    EnterCriticalSection(&request_lock);
    flag = true;
    LeaveCriticalSection(&request_lock);
    SetEvent(request_event);
}

}

void worker_start() {
    InitializeCriticalSection(&request_lock);
    request_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HANDLE worker = CreateThread(nullptr, 0, worker_thread, nullptr, 0, nullptr);
    if (worker) {
        CloseHandle(worker);
    }
}

void request_scan() {
    set_flag(scan_wanted);
}

void request_known() {
    set_flag(known_wanted);
}

void request_log() {
    set_flag(log_wanted);
}

void request_status_now() {
    SetEvent(request_event);
}

void request_command(const char *command) {
    EnterCriticalSection(&request_lock);
    if (command_queue_count < command_queue_limit) {
        copy_text(command_queue[command_queue_count++], command, command_bytes);
    }
    LeaveCriticalSection(&request_lock);
    SetEvent(request_event);
}

void build_command(char *command, const char *verb, const char *first, const char *second) {
    int length = 0;
    const char *parts[3] = { verb, first, second };
    for (const char *part : parts) {
        if (!part) {
            continue;
        }
        if (length && length + 1 < command_bytes) {
            command[length++] = ' ';
        }
        for (int index = 0; part[index] && length + 1 < command_bytes; index++) {
            command[length++] = part[index];
        }
    }
    command[length] = 0;
}
