// Application-wide state shared by every part of Brainwave WiFi: where it is installed, its window and its log file.
#include "app.h"

wchar_t program_directory[MAX_PATH] = L"";
HINSTANCE program_instance = nullptr;
HWND main_window = nullptr;

namespace {

wchar_t log_path[MAX_PATH] = L"";

}

void app_find_program_directory() {
    GetModuleFileNameW(nullptr, program_directory, MAX_PATH);
    wchar_t *last_separator = nullptr;
    for (wchar_t *cursor = program_directory; *cursor; cursor++) {
        if (*cursor == L'\\') {
            last_separator = cursor;
        }
    }
    if (last_separator) {
        *last_separator = 0;
    } else {
        lstrcpyW(program_directory, L"\\Storage Card\\brainwave");
    }
    wsprintfW(log_path, L"%s\\brainwave.log", program_directory);
}

void append_log(const wchar_t *text) {
    HANDLE file = CreateFileW(log_path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    char line[256];
    int length = 0;
    wchar_t stamp[16];
    wsprintfW(stamp, L"%08u ", static_cast<unsigned>(GetTickCount()));
    for (int index = 0; stamp[index]; index++) {
        line[length++] = static_cast<char>(stamp[index]);
    }
    while (*text && length < static_cast<int>(sizeof(line)) - 2) {
        line[length++] = *text < 128 ? static_cast<char>(*text) : '?';
        text++;
    }
    line[length++] = '\r';
    line[length++] = '\n';
    DWORD written = 0;
    SetFilePointer(file, 0, nullptr, FILE_END);
    WriteFile(file, line, length, &written, nullptr);
    CloseHandle(file);
}
