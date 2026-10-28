// Sends brainwave.bin to the module on a worker thread: reads and hashes the file, streams it with the update command and reports
// progress.
#include "firmware_update.h"

#include <string.h>

#include "app.h"
#include "module.h"
#include "sha256.h"

namespace {

constexpr const char *version_marker = "BRAINWAVE-FIRMWARE ";
constexpr DWORD maximum_image_bytes = 4 * 1024 * 1024;
constexpr DWORD read_chunk_bytes = 16384;

firmware_file_info cached = {};
DWORD cached_size = 0xffffffff;
FILETIME cached_time = {};
volatile LONG running = 0;
unsigned long update_address = 0;
int posted_percent = -1;

void image_path(wchar_t *path) {
    wsprintfW(path, L"%s\\brainwave.bin", program_directory);
}

void find_version(const BYTE *data, DWORD size, wchar_t *version) {
    version[0] = 0;
    DWORD marker_length = static_cast<DWORD>(strlen(version_marker));
    for (DWORD offset = 0; offset + marker_length < size; offset++) {
        if (data[offset] != 'B' || memcmp(data + offset, version_marker, marker_length)) {
            continue;
        }
        DWORD start = offset + marker_length;
        int length = 0;
        while (start + length < size && data[start + length] && length + 1 < firmware_version_characters) {
            BYTE character = data[start + length];
            if (character < 0x20 || character > 0x7e) {
                length = 0;
                break;
            }
            length++;
        }
        if (length && start + length < size && !data[start + length]) {
            for (int index = 0; index < length; index++) {
                version[index] = data[start + index];
            }
            version[length] = 0;
            return;
        }
    }
}

BYTE *read_image(DWORD &size) {
    wchar_t path[MAX_PATH];
    image_path(path);
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return nullptr;
    }
    size = GetFileSize(file, nullptr);
    BYTE *data = size && size <= maximum_image_bytes ? static_cast<BYTE *>(LocalAlloc(LMEM_FIXED, size)) : nullptr;
    DWORD total = 0;
    while (data && total < size) {
        DWORD wanted = size - total < read_chunk_bytes ? size - total : read_chunk_bytes;
        DWORD got = 0;
        if (!ReadFile(file, data + total, wanted, &got, nullptr) || !got) {
            LocalFree(data);
            data = nullptr;
            break;
        }
        total += got;
    }
    CloseHandle(file);
    return data;
}

void post_report(update_phase phase, int percent, const char *error) {
    update_report *report = static_cast<update_report *>(LocalAlloc(LPTR, sizeof(update_report)));
    if (!report) {
        return;
    }
    report->phase = phase;
    report->percent = percent;
    if (error) {
        int index = 0;
        for (; index + 1 < static_cast<int>(sizeof(report->error)) && error[index]; index++) {
            report->error[index] = error[index];
        }
        report->error[index] = 0;
    }
    if (!PostMessageW(main_window, update_message, 0, reinterpret_cast<LPARAM>(report))) {
        LocalFree(report);
    }
}

void report_progress(DWORD sent, DWORD total, void *) {
    int percent = static_cast<int>(static_cast<unsigned long long>(sent) * 100 / total);
    if (percent != posted_percent) {
        posted_percent = percent;
        post_report(sent == total ? update_phase::writing : update_phase::sending, percent, nullptr);
    }
}

DWORD WINAPI update_thread(LPVOID) {
    post_report(update_phase::checking, 0, nullptr);
    DWORD size = 0;
    BYTE *image = read_image(size);
    if (!image) {
        post_report(update_phase::failed, 0, "could not read brainwave.bin");
        InterlockedExchange(&running, 0);
        return 0;
    }
    sha256_context context;
    sha256_start(context);
    sha256_add(context, image, size);
    uint8_t digest[32];
    char digest_hex[65];
    sha256_finish(context, digest);
    sha256_hex(digest, digest_hex);
    posted_percent = -1;
    char error[64] = "";
    bool ok = module_update(update_address, image, size, digest_hex, report_progress, nullptr, error, sizeof(error));
    LocalFree(image);
    bool sent_without_answer = !ok && !strcmp(error, "no answer after sending");
    if (sent_without_answer) {
        append_log(L"update: no answer after sending; watching for the module to restart");
    }
    post_report(ok || sent_without_answer ? update_phase::restarting : update_phase::failed, 100, ok || sent_without_answer ? nullptr : error);
    InterlockedExchange(&running, 0);
    return 0;
}

}

const firmware_file_info &firmware_probe() {
    wchar_t path[MAX_PATH];
    image_path(path);
    WIN32_FIND_DATAW found = {};
    HANDLE search = FindFirstFileW(path, &found);
    if (search == INVALID_HANDLE_VALUE) {
        cached.present = false;
        cached_size = 0xffffffff;
        return cached;
    }
    FindClose(search);
    if (found.nFileSizeLow == cached_size && found.ftLastWriteTime.dwLowDateTime == cached_time.dwLowDateTime &&
        found.ftLastWriteTime.dwHighDateTime == cached_time.dwHighDateTime) {
        return cached;
    }
    cached_size = found.nFileSizeLow;
    cached_time = found.ftLastWriteTime;
    cached.present = true;
    cached.size = found.nFileSizeLow;
    DWORD size = 0;
    BYTE *image = read_image(size);
    cached.version[0] = 0;
    if (image) {
        find_version(image, size, cached.version);
        LocalFree(image);
    }
    if (!cached.version[0]) {
        lstrcpyW(cached.version, L"unknown version");
    }
    return cached;
}

bool firmware_update_start(unsigned long address) {
    if (InterlockedCompareExchange(&running, 1, 0) != 0) {
        return false;
    }
    update_address = address;
    HANDLE thread = CreateThread(nullptr, 0, update_thread, nullptr, 0, nullptr);
    if (!thread) {
        InterlockedExchange(&running, 0);
        return false;
    }
    CloseHandle(thread);
    return true;
}

bool firmware_update_running() {
    return running != 0;
}
