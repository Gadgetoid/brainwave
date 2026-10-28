// Timestamped log lines in a 4 KB ring in uninitialised RAM, also written to UART0.
#include "log.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "pico/time.h"

namespace {

constexpr size_t ring_bytes = 4096;
constexpr uint32_t persistent_magic = 0x424c4f47;

struct PersistentLog {
    uint32_t magic;
    uint32_t start;
    uint32_t length;
    char text[ring_bytes];
};

PersistentLog persistent __attribute__((section(".uninitialized_data.persistent_log")));

void ring_append(const char *text, size_t length) {
    for (size_t index = 0; index < length; index++) {
        size_t position = (persistent.start + persistent.length) % ring_bytes;
        persistent.text[position] = text[index];
        if (persistent.length < ring_bytes) {
            persistent.length++;
        } else {
            persistent.start = (persistent.start + 1) % ring_bytes;
        }
    }
}

}

void log_restore() {
    if (persistent.magic != persistent_magic || persistent.start >= ring_bytes || persistent.length > ring_bytes) {
        persistent.magic = persistent_magic;
        persistent.start = 0;
        persistent.length = 0;
    }
}

void log_raw(const char *text) {
    size_t length = 0;
    while (text[length]) {
        length++;
    }
    ring_append(text, length);
}

void log_line(const char *format, ...) {
    char line[160];
    uint32_t milliseconds = to_ms_since_boot(get_absolute_time());
    int prefix = snprintf(line, sizeof(line), "%6lu.%03lu ", static_cast<unsigned long>(milliseconds / 1000),
                          static_cast<unsigned long>(milliseconds % 1000));
    va_list arguments;
    va_start(arguments, format);
    int body = vsnprintf(line + prefix, sizeof(line) - prefix - 1, format, arguments);
    va_end(arguments);
    size_t length = prefix + (body < 0 ? 0 : body);
    if (length > sizeof(line) - 2) {
        length = sizeof(line) - 2;
    }
    line[length++] = '\n';
    line[length] = '\0';
    fputs(line, stdout);
    ring_append(line, length);
}

size_t log_copy(char *destination, size_t capacity) {
    size_t length = persistent.length < capacity ? persistent.length : capacity;
    size_t skip = persistent.length - length;
    for (size_t index = 0; index < length; index++) {
        destination[index] = persistent.text[(persistent.start + skip + index) % ring_bytes];
    }
    return length;
}
