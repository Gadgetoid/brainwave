#include "sntp.h"

#include <winsock.h>
#include <string.h>

namespace {

constexpr unsigned short ntp_port = 123;
constexpr int packet_bytes = 48;
constexpr int attempt_limit = 3;
constexpr DWORD reply_timeout_milliseconds = 3000;
constexpr BYTE ntp_version = 4;
constexpr BYTE client_mode = 3;
constexpr BYTE server_mode = 4;
constexpr BYTE unsynchronised_leap = 3;
constexpr int highest_stratum = 15;
constexpr int stratum_offset = 1;
constexpr int originate_offset = 24;
constexpr int transmit_offset = 40;
constexpr ULONGLONG filetime_ticks_per_second = 10000000;
constexpr ULONGLONG filetime_ticks_per_millisecond = 10000;
constexpr ULONGLONG ntp_epoch_after_filetime_epoch_seconds = 9435484800ULL;
constexpr ULONGLONG ntp_era_seconds = 0x100000000ULL;
constexpr DWORD ntp_era_split_seconds = 0x80000000UL;
constexpr WORD earliest_plausible_year = 2025;
constexpr WORD latest_plausible_year = 2099;

void copy_error(char *error, const char *text, int error_bytes) {
    int index = 0;
    for (; index + 1 < error_bytes && text[index]; index++) {
        error[index] = text[index];
    }
    error[index] = 0;
}

DWORD read_word(const BYTE *bytes) {
    return static_cast<DWORD>(bytes[0]) << 24 | static_cast<DWORD>(bytes[1]) << 16 | static_cast<DWORD>(bytes[2]) << 8 | bytes[3];
}

void write_word(BYTE *bytes, DWORD value) {
    bytes[0] = static_cast<BYTE>(value >> 24);
    bytes[1] = static_cast<BYTE>(value >> 16);
    bytes[2] = static_cast<BYTE>(value >> 8);
    bytes[3] = static_cast<BYTE>(value);
}

bool resolve(const char *server, unsigned long &address) {
    address = inet_addr(server);
    if (address != INADDR_NONE) {
        return true;
    }
    hostent *host = gethostbyname(server);
    if (!host || host->h_addrtype != AF_INET || !host->h_addr_list[0]) {
        return false;
    }
    memcpy(&address, host->h_addr_list[0], sizeof(address));
    return true;
}

bool wait_readable(SOCKET connection, DWORD milliseconds) {
    fd_set ready;
    FD_ZERO(&ready);
    FD_SET(connection, &ready);
    timeval timeout;
    timeout.tv_sec = milliseconds / 1000;
    timeout.tv_usec = (milliseconds % 1000) * 1000;
    return select(0, &ready, nullptr, nullptr, &timeout) > 0 && FD_ISSET(connection, &ready);
}

ULONGLONG reply_time(const BYTE *reply, DWORD round_trip_milliseconds) {
    DWORD seconds = read_word(reply + transmit_offset);
    DWORD fraction = read_word(reply + transmit_offset + 4);
    ULONGLONG ntp_seconds = seconds;
    if (seconds < ntp_era_split_seconds) {
        ntp_seconds += ntp_era_seconds;
    }
    return (ntp_seconds + ntp_epoch_after_filetime_epoch_seconds) * filetime_ticks_per_second +
           ((static_cast<ULONGLONG>(fraction) * filetime_ticks_per_second) >> 32) +
           static_cast<ULONGLONG>(round_trip_milliseconds / 2) * filetime_ticks_per_millisecond;
}

bool plausible(ULONGLONG utc_time) {
    FILETIME file;
    file.dwLowDateTime = static_cast<DWORD>(utc_time);
    file.dwHighDateTime = static_cast<DWORD>(utc_time >> 32);
    SYSTEMTIME time;
    if (!FileTimeToSystemTime(&file, &time)) {
        return false;
    }
    return time.wYear >= earliest_plausible_year && time.wYear <= latest_plausible_year;
}

const char *check_reply(const BYTE *reply) {
    BYTE leap = reply[0] >> 6;
    BYTE mode = reply[0] & 7;
    int stratum = reply[stratum_offset];
    if (mode != server_mode) {
        return "not an NTP server reply";
    }
    if (stratum == 0) {
        return "the server refused the request";
    }
    if (stratum > highest_stratum || leap == unsynchronised_leap) {
        return "the server's clock is not synchronised";
    }
    if (!read_word(reply + transmit_offset) && !read_word(reply + transmit_offset + 4)) {
        return "the reply had no time";
    }
    return nullptr;
}

}

bool sntp_query(const char *server, sntp_result &result, char *error, int error_bytes) {
    unsigned long address = 0;
    if (!server[0]) {
        copy_error(error, "no server name", error_bytes);
        return false;
    }
    if (!resolve(server, address)) {
        copy_error(error, "could not look up the server", error_bytes);
        return false;
    }
    SOCKET connection = socket(AF_INET, SOCK_DGRAM, 0);
    if (connection == INVALID_SOCKET) {
        copy_error(error, "no socket", error_bytes);
        return false;
    }
    sockaddr_in destination = {};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(ntp_port);
    destination.sin_addr.s_addr = address;
    const char *failure = "no reply from the server";
    bool ok = false;
    for (int attempt = 0; attempt < attempt_limit && !ok; attempt++) {
        BYTE request[packet_bytes] = {};
        request[0] = static_cast<BYTE>(ntp_version << 3 | client_mode);
        DWORD sent_tick = GetTickCount();
        write_word(request + transmit_offset, sent_tick);
        write_word(request + transmit_offset + 4, sent_tick * 2654435761UL + static_cast<DWORD>(attempt));
        if (sendto(connection, reinterpret_cast<const char *>(request), packet_bytes, 0, reinterpret_cast<const sockaddr *>(&destination),
                   sizeof(destination)) != packet_bytes) {
            failure = "could not send the request";
            Sleep(reply_timeout_milliseconds);
            continue;
        }
        for (;;) {
            DWORD waited = GetTickCount() - sent_tick;
            if (waited >= reply_timeout_milliseconds || !wait_readable(connection, reply_timeout_milliseconds - waited)) {
                break;
            }
            BYTE reply[packet_bytes * 2];
            sockaddr_in source = {};
            int source_bytes = sizeof(source);
            int length = recvfrom(connection, reinterpret_cast<char *>(reply), sizeof(reply), 0, reinterpret_cast<sockaddr *>(&source),
                                  &source_bytes);
            if (length < packet_bytes || source.sin_addr.s_addr != address ||
                memcmp(reply + originate_offset, request + transmit_offset, 8)) {
                continue;
            }
            DWORD round_trip = GetTickCount() - sent_tick;
            const char *problem = check_reply(reply);
            if (problem) {
                failure = problem;
                break;
            }
            ULONGLONG utc_time = reply_time(reply, round_trip);
            if (!plausible(utc_time)) {
                failure = "the server's time is implausible";
                break;
            }
            result.utc_time = utc_time;
            result.stratum = reply[stratum_offset];
            result.round_trip_milliseconds = round_trip;
            ok = true;
            break;
        }
    }
    closesocket(connection);
    if (!ok) {
        copy_error(error, failure, error_bytes);
    }
    return ok;
}
