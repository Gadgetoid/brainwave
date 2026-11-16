#pragma once

#include <windows.h>

struct sntp_result {
    ULONGLONG utc_time;
    int stratum;
    DWORD round_trip_milliseconds;
};

bool sntp_query(const char *server, sntp_result &result, char *error, int error_bytes);
