// Client for the module's control protocol (docs/protocol.md) over TCP to the Brain's gateway: status, scan, known, log, commands
// and firmware updates.
#include "module.h"

#include <winsock.h>
#include <string.h>

#include "text.h"

namespace {

constexpr const char *fallback_address = "10.88.0.1";
constexpr DWORD connect_timeout_milliseconds = 2000;
constexpr DWORD status_timeout_milliseconds = 3000;
constexpr DWORD scan_timeout_milliseconds = 10000;
constexpr DWORD command_timeout_milliseconds = 3000;
constexpr int response_bytes = 8192;
constexpr DWORD update_ready_timeout_milliseconds = 10000;
constexpr DWORD update_result_timeout_milliseconds = 30000;
constexpr DWORD update_send_timeout_milliseconds = 15000;
constexpr DWORD update_chunk_bytes = 4096;

void copy_field(char *destination, const char *source, int destination_bytes) {
    int index = 0;
    for (; index + 1 < destination_bytes && source[index]; index++) {
        destination[index] = source[index];
    }
    destination[index] = 0;
}

int parse_int(const char *text) {
    bool negative = *text == '-';
    if (negative) {
        text++;
    }
    int value = 0;
    while (*text >= '0' && *text <= '9') {
        value = value * 10 + (*text++ - '0');
    }
    return negative ? -value : value;
}

char *next_token(char *&cursor) {
    while (*cursor == ' ') {
        cursor++;
    }
    if (!*cursor) {
        return nullptr;
    }
    char *start = cursor;
    while (*cursor && *cursor != ' ') {
        cursor++;
    }
    if (*cursor) {
        *cursor++ = 0;
    }
    return start;
}

char *next_line(char *&cursor) {
    if (!*cursor) {
        return nullptr;
    }
    char *start = cursor;
    while (*cursor && *cursor != '\n') {
        cursor++;
    }
    if (*cursor) {
        *cursor++ = 0;
    }
    int length = static_cast<int>(strlen(start));
    if (length && start[length - 1] == '\r') {
        start[length - 1] = 0;
    }
    return start;
}

bool wait_socket(SOCKET connection, bool for_write, DWORD milliseconds) {
    fd_set ready;
    fd_set failed;
    FD_ZERO(&ready);
    FD_ZERO(&failed);
    FD_SET(connection, &ready);
    FD_SET(connection, &failed);
    timeval timeout;
    timeout.tv_sec = milliseconds / 1000;
    timeout.tv_usec = (milliseconds % 1000) * 1000;
    int result = for_write ? select(0, nullptr, &ready, &failed, &timeout) : select(0, &ready, nullptr, &failed, &timeout);
    return result > 0 && FD_ISSET(connection, &ready) && !FD_ISSET(connection, &failed);
}

char exchange_failure[64];

bool response_complete(const char *response, int length) {
    if (length < 3 || response[length - 1] != '\n') {
        return false;
    }
    int start = length - 1;
    while (start > 0 && response[start - 1] != '\n') {
        start--;
    }
    int line_length = length - 1 - start;
    if (line_length && response[start + line_length - 1] == '\r') {
        line_length--;
    }
    const char *line = response + start;
    return (line_length == 2 && line[0] == 'o' && line[1] == 'k') || (line_length >= 6 && !strncmp(line, "error ", 6));
}

void note_failure(const char *stage, int value) {
    char text[64];
    int length = 0;
    while (stage[length] && length < 40) {
        text[length] = stage[length];
        length++;
    }
    text[length++] = ' ';
    unsigned magnitude = value < 0 ? static_cast<unsigned>(-value) : static_cast<unsigned>(value);
    char digits[12];
    int count = 0;
    do {
        digits[count++] = static_cast<char>('0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude);
    if (value < 0) {
        text[length++] = '-';
    }
    while (count) {
        text[length++] = digits[--count];
    }
    text[length] = 0;
    copy_field(exchange_failure, text, sizeof(exchange_failure));
}

SOCKET open_connection(unsigned long address) {
    SOCKET connection = socket(AF_INET, SOCK_STREAM, 0);
    if (connection == INVALID_SOCKET) {
        return INVALID_SOCKET;
    }
    sockaddr_in destination = {};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(module_port);
    destination.sin_addr.s_addr = address;
    unsigned long non_blocking = 1;
    ioctlsocket(connection, FIONBIO, &non_blocking);
    connect(connection, reinterpret_cast<sockaddr *>(&destination), sizeof(destination));
    int socket_error = 0;
    int option_length = sizeof(socket_error);
    if (!wait_socket(connection, true, connect_timeout_milliseconds) ||
        getsockopt(connection, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&socket_error), &option_length) || socket_error) {
        closesocket(connection);
        return INVALID_SOCKET;
    }
    non_blocking = 0;
    ioctlsocket(connection, FIONBIO, &non_blocking);
    return connection;
}

bool read_reply_line(SOCKET connection, DWORD timeout_milliseconds, char *line, int line_bytes) {
    int length = 0;
    DWORD started = GetTickCount();
    for (;;) {
        DWORD elapsed = GetTickCount() - started;
        if (elapsed >= timeout_milliseconds || !wait_socket(connection, false, timeout_milliseconds - elapsed)) {
            line[length] = 0;
            return false;
        }
        char character = 0;
        if (recv(connection, &character, 1, 0) != 1) {
            line[length] = 0;
            return false;
        }
        if (character == '\n') {
            if (length && line[length - 1] == '\r') {
                length--;
            }
            line[length] = 0;
            return true;
        }
        if (length + 1 < line_bytes) {
            line[length++] = character;
        }
    }
}

bool send_all(SOCKET connection, const char *data, int length) {
    while (length > 0) {
        if (!wait_socket(connection, true, update_send_timeout_milliseconds)) {
            return false;
        }
        int sent = send(connection, data, length, 0);
        if (sent <= 0) {
            return false;
        }
        data += sent;
        length -= sent;
    }
    return true;
}

void write_decimal(char *text, DWORD value) {
    char digits[12];
    int count = 0;
    do {
        digits[count++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value);
    int length = 0;
    while (count) {
        text[length++] = digits[--count];
    }
    text[length] = 0;
}

int exchange(unsigned long address, const char *command, DWORD timeout_milliseconds, char *response, int bytes) {
    exchange_failure[0] = 0;
    SOCKET connection = socket(AF_INET, SOCK_STREAM, 0);
    if (connection == INVALID_SOCKET) {
        note_failure("socket", WSAGetLastError());
        return -1;
    }
    sockaddr_in destination = {};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(module_port);
    destination.sin_addr.s_addr = address;
    unsigned long non_blocking = 1;
    ioctlsocket(connection, FIONBIO, &non_blocking);
    connect(connection, reinterpret_cast<sockaddr *>(&destination), sizeof(destination));
    int length = -1;
    if (wait_socket(connection, true, connect_timeout_milliseconds)) {
        int socket_error = 0;
        int option_length = sizeof(socket_error);
        getsockopt(connection, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&socket_error), &option_length);
        non_blocking = 0;
        ioctlsocket(connection, FIONBIO, &non_blocking);
        int command_length = static_cast<int>(strlen(command));
        if (socket_error) {
            note_failure("connect", socket_error);
        }
        if (!socket_error && send(connection, command, command_length, 0) == command_length && send(connection, "\n", 1, 0) == 1) {
            length = 0;
            DWORD started = GetTickCount();
            for (;;) {
                DWORD elapsed = GetTickCount() - started;
                if (elapsed >= timeout_milliseconds || length >= bytes - 1 || !wait_socket(connection, false, timeout_milliseconds - elapsed)) {
                    note_failure("receive wait, bytes", length);
                    length = -1;
                    break;
                }
                int received = recv(connection, response + length, bytes - 1 - length, 0);
                if (received <= 0) {
                    if (received < 0) {
                        note_failure("recv error", WSAGetLastError());
                    }
                    break;
                }
                length += received;
                if (response_complete(response, length)) {
                    break;
                }
            }
        }
    }
    if (length < 0 && !exchange_failure[0]) {
        note_failure("connect wait", WSAGetLastError());
    }
    closesocket(connection);
    if (length >= 0) {
        response[length] = 0;
    }
    return length;
}

bool final_line_ok(const char *line, char *error, int error_bytes) {
    if (line && !strcmp(line, "ok")) {
        return true;
    }
    if (error) {
        copy_field(error, line && starts_with(line, "error ") ? line + 6 : "no answer", error_bytes);
    }
    return false;
}

}

unsigned long module_address() {
    HKEY key;
    unsigned long address = INADDR_NONE;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Comm\\RndisFn1\\Parms\\TcpIp", 0, 0, &key) == ERROR_SUCCESS) {
        wchar_t gateways[64];
        DWORD size = sizeof(gateways) - sizeof(wchar_t) * 2;
        DWORD type = 0;
        memset(gateways, 0, sizeof(gateways));
        if (RegQueryValueExW(key, L"DhcpDefaultGateway", nullptr, &type, reinterpret_cast<BYTE *>(gateways), &size) == ERROR_SUCCESS &&
            (type == REG_MULTI_SZ || type == REG_SZ)) {
            char first[32];
            wide_to_ascii(gateways, first, sizeof(first));
            address = inet_addr(first);
        }
        RegCloseKey(key);
    }
    if (address == INADDR_NONE || address == 0) {
        address = inet_addr(fallback_address);
    }
    return address;
}

bool module_query_status(unsigned long address, module_status &status) {
    static char response[response_bytes];
    memset(&status, 0, sizeof(status));
    int received = exchange(address, "status", status_timeout_milliseconds, response, sizeof(response));
    copy_field(status.failure, exchange_failure, sizeof(status.failure));
    if (received < 0) {
        return false;
    }
    char *cursor = response;
    char *last = nullptr;
    while (char *line = next_line(cursor)) {
        last = line;
        char *equals = strchr(line, '=');
        if (!equals) {
            continue;
        }
        *equals = 0;
        const char *value = equals + 1;
        if (!strcmp(line, "version")) {
            copy_field(status.version, value, sizeof(status.version));
        } else if (!strcmp(line, "wifi")) {
            copy_field(status.wifi, value, sizeof(status.wifi));
        } else if (!strcmp(line, "ssid")) {
            copy_field(status.ssid_hex, value, sizeof(status.ssid_hex));
            hex_to_wide(value, status.ssid, module_ssid_characters);
        } else if (!strcmp(line, "security")) {
            copy_field(status.security, value, sizeof(status.security));
        } else if (!strcmp(line, "channel")) {
            copy_field(status.channel, value, sizeof(status.channel));
        } else if (!strcmp(line, "bssid")) {
            copy_field(status.bssid, value, sizeof(status.bssid));
        } else if (!strcmp(line, "netmask")) {
            copy_field(status.netmask, value, sizeof(status.netmask));
        } else if (!strcmp(line, "mac")) {
            copy_field(status.mac, value, sizeof(status.mac));
        } else if (!strcmp(line, "battery")) {
            status.battery_millivolts = *value == '-' ? 0 : parse_int(value);
        } else if (!strcmp(line, "uptime")) {
            status.has_uptime = *value != '-';
            status.uptime_seconds = parse_int(value);
        } else if (!strcmp(line, "slot")) {
            copy_field(status.slot, value, sizeof(status.slot));
        } else if (!strcmp(line, "debug-forward")) {
            status.debug_forward = !strcmp(value, "on");
        } else if (!strcmp(line, "ip")) {
            copy_field(status.ip, value, sizeof(status.ip));
        } else if (!strcmp(line, "gateway")) {
            copy_field(status.gateway, value, sizeof(status.gateway));
        } else if (!strcmp(line, "dns")) {
            copy_field(status.dns, value, sizeof(status.dns));
        } else if (!strcmp(line, "rssi")) {
            copy_field(status.rssi, value, sizeof(status.rssi));
        } else if (!strcmp(line, "brain")) {
            copy_field(status.brain, value, sizeof(status.brain));
        } else if (!strcmp(line, "known")) {
            status.known = parse_int(value);
        }
    }
    status.reachable = final_line_ok(last, nullptr, 0);
    if (!status.reachable && !status.failure[0]) {
        note_failure("no ok line, bytes", received);
        copy_field(status.failure, exchange_failure, sizeof(status.failure));
    }
    return status.reachable;
}

bool module_query_scan(unsigned long address, module_scan &scan) {
    static char response[response_bytes];
    memset(&scan, 0, sizeof(scan));
    if (exchange(address, "scan", scan_timeout_milliseconds, response, sizeof(response)) < 0) {
        copy_field(scan.error, "module not reachable", sizeof(scan.error));
        return false;
    }
    char *cursor = response;
    char *last = nullptr;
    while (char *line = next_line(cursor)) {
        last = line;
        if (!starts_with(line, "ap ") || scan.count >= module_network_limit) {
            continue;
        }
        char *fields = line + 3;
        char *rssi = next_token(fields);
        char *security = next_token(fields);
        char *channel = next_token(fields);
        char *saved = next_token(fields);
        char *ssid = next_token(fields);
        if (!ssid) {
            ssid = saved;
            saved = nullptr;
        }
        if (!ssid) {
            continue;
        }
        module_network &network = scan.networks[scan.count];
        copy_field(network.ssid_hex, ssid, sizeof(network.ssid_hex));
        if (!hex_to_wide(ssid, network.ssid, module_ssid_characters) || !network.ssid[0]) {
            continue;
        }
        network.rssi = parse_int(rssi);
        copy_field(network.security, security, sizeof(network.security));
        network.channel = parse_int(channel);
        network.saved = saved && saved[0] == '1';
        scan.count++;
    }
    scan.ok = final_line_ok(last, scan.error, sizeof(scan.error));
    return scan.ok;
}

bool module_query_known(unsigned long address, module_known &known) {
    static char response[response_bytes];
    memset(&known, 0, sizeof(known));
    if (exchange(address, "known", command_timeout_milliseconds, response, sizeof(response)) < 0) {
        return false;
    }
    char *cursor = response;
    char *last = nullptr;
    while (char *line = next_line(cursor)) {
        last = line;
        if (!starts_with(line, "net ") || known.count >= module_network_limit) {
            continue;
        }
        copy_field(known.ssid_hex[known.count], line + 4, module_ssid_hex_bytes);
        if (hex_to_wide(line + 4, known.ssid[known.count], module_ssid_characters) && known.ssid[known.count][0]) {
            known.count++;
        }
    }
    known.ok = final_line_ok(last, nullptr, 0);
    return known.ok;
}

bool module_query_log(unsigned long address, char *text, int text_bytes) {
    static char response[response_bytes];
    text[0] = 0;
    if (exchange(address, "log", command_timeout_milliseconds, response, sizeof(response)) < 0) {
        return false;
    }
    int length = 0;
    char *cursor = response;
    char *last = nullptr;
    char *previous = nullptr;
    while (char *line = next_line(cursor)) {
        if (previous) {
            for (int index = 0; previous[index] && length + 3 < text_bytes; index++) {
                text[length++] = previous[index];
            }
            if (length + 3 < text_bytes) {
                text[length++] = '\r';
                text[length++] = '\n';
            }
        }
        previous = line;
        last = line;
    }
    text[length] = 0;
    return final_line_ok(last, nullptr, 0);
}

bool module_send_command(unsigned long address, const char *command, char *error, int error_bytes) {
    static char response[1024];
    if (exchange(address, command, command_timeout_milliseconds, response, sizeof(response)) < 0) {
        copy_field(error, "module not reachable", error_bytes);
        return false;
    }
    char *cursor = response;
    char *last = nullptr;
    while (char *line = next_line(cursor)) {
        last = line;
    }
    return final_line_ok(last, error, error_bytes);
}

bool module_update(unsigned long address, const BYTE *image, DWORD size, const char *digest_hex, module_update_progress progress, void *context,
                   char *error, int error_bytes) {
    SOCKET connection = open_connection(address);
    if (connection == INVALID_SOCKET) {
        copy_field(error, "module not reachable", error_bytes);
        return false;
    }
    char command[128] = "update ";
    write_decimal(command + strlen(command), size);
    int length = static_cast<int>(strlen(command));
    command[length++] = ' ';
    copy_field(command + length, digest_hex, static_cast<int>(sizeof(command)) - length - 2);
    length = static_cast<int>(strlen(command));
    command[length++] = '\n';
    command[length] = 0;
    char line[96];
    bool ok = send_all(connection, command, length);
    if (ok && (!read_reply_line(connection, update_ready_timeout_milliseconds, line, sizeof(line)) || strcmp(line, "ready"))) {
        copy_field(error, line[0] ? (starts_with(line, "error ") ? line + 6 : line) : "no answer to the update request", error_bytes);
        closesocket(connection);
        return false;
    }
    DWORD sent = 0;
    while (ok && sent < size) {
        DWORD chunk = size - sent < update_chunk_bytes ? size - sent : update_chunk_bytes;
        ok = send_all(connection, reinterpret_cast<const char *>(image + sent), static_cast<int>(chunk));
        if (ok) {
            sent += chunk;
            if (progress) {
                progress(sent, size, context);
            }
        }
    }
    if (!ok) {
        copy_field(error, "the connection dropped while sending", error_bytes);
        closesocket(connection);
        return false;
    }
    bool answered = read_reply_line(connection, update_result_timeout_milliseconds, line, sizeof(line));
    closesocket(connection);
    if (answered && !strcmp(line, "ok")) {
        return true;
    }
    copy_field(error, !answered ? "no answer after sending" : (starts_with(line, "error ") ? line + 6 : line), error_bytes);
    return false;
}
