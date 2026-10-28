// A small text log kept in RAM that survives watchdog and fault reboots.
#pragma once

#include <cstddef>

void log_line(const char *format, ...) __attribute__((format(printf, 1, 2)));
size_t log_copy(char *destination, size_t capacity);
void log_restore();
void log_raw(const char *text);
