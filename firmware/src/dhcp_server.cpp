// Leases 10.88.0.2 to the Brain, with Brainwave as router and DNS server.
#include "dhcp_server.h"

#include <cstring>

#include "lwip/udp.h"

#include "brain_link.h"
#include "log.h"
#include "napt.h"

namespace {

constexpr uint16_t server_port = 67;
constexpr uint16_t client_port = 68;
constexpr size_t bootp_bytes = 236;
constexpr uint32_t magic_cookie = 0x63825363;
constexpr uint8_t option_pad = 0;
constexpr uint8_t option_subnet_mask = 1;
constexpr uint8_t option_router = 3;
constexpr uint8_t option_dns = 6;
constexpr uint8_t option_requested_address = 50;
constexpr uint8_t option_lease_time = 51;
constexpr uint8_t option_message_type = 53;
constexpr uint8_t option_server_identifier = 54;
constexpr uint8_t option_end = 255;
constexpr uint8_t discover = 1;
constexpr uint8_t offer = 2;
constexpr uint8_t request_message = 3;
constexpr uint8_t acknowledge = 5;
constexpr uint8_t refuse = 6;
constexpr uint8_t inform = 8;
constexpr uint32_t lease_seconds = 24 * 60 * 60;

udp_pcb *server = nullptr;
uint32_t leased = 0;

const uint8_t *find_option(const uint8_t *options, size_t length, uint8_t code, uint8_t &option_length) {
    size_t index = 0;
    while (index < length) {
        uint8_t current = options[index];
        if (current == option_end) {
            break;
        }
        if (current == option_pad) {
            index++;
            continue;
        }
        if (index + 1 >= length || index + 2 + options[index + 1] > length) {
            break;
        }
        if (current == code) {
            option_length = options[index + 1];
            return options + index + 2;
        }
        index += 2 + options[index + 1];
    }
    return nullptr;
}

uint8_t *add_option(uint8_t *cursor, uint8_t code, uint8_t length, const void *value) {
    cursor[0] = code;
    cursor[1] = length;
    memcpy(cursor + 2, value, length);
    return cursor + 2 + length;
}

uint8_t *add_address(uint8_t *cursor, uint8_t code, uint32_t address) {
    uint8_t bytes[4];
    napt::write32(bytes, address);
    return add_option(cursor, code, 4, bytes);
}

void reply(const uint8_t *request, uint8_t type, uint32_t offered) {
    uint8_t message[bootp_bytes + 64] = {};
    message[0] = 2;
    message[1] = request[1];
    message[2] = request[2];
    memcpy(message + 4, request + 4, 4);
    memcpy(message + 10, request + 10, 2);
    napt::write32(message + 16, offered);
    napt::write32(message + 20, brain_link_router);
    memcpy(message + 28, request + 28, 16);
    uint8_t *cursor = message + bootp_bytes;
    napt::write32(cursor, magic_cookie);
    cursor += 4;
    cursor = add_option(cursor, option_message_type, 1, &type);
    cursor = add_address(cursor, option_server_identifier, brain_link_router);
    if (type != refuse) {
        uint8_t lease[4];
        napt::write32(lease, lease_seconds);
        cursor = add_option(cursor, option_lease_time, 4, lease);
        cursor = add_address(cursor, option_subnet_mask, brain_link_mask);
        cursor = add_address(cursor, option_router, brain_link_router);
        cursor = add_address(cursor, option_dns, brain_link_router);
    }
    *cursor++ = option_end;
    size_t length = static_cast<size_t>(cursor - message);
    if (length < 300) {
        length = 300;
    }
    pbuf *packet = pbuf_alloc(PBUF_TRANSPORT, static_cast<u16_t>(length), PBUF_RAM);
    if (!packet) {
        return;
    }
    pbuf_take(packet, message, static_cast<u16_t>(length));
    udp_sendto_if(server, packet, IP4_ADDR_BROADCAST, client_port, brain_link_netif());
    pbuf_free(packet);
}

void received(void *, udp_pcb *, pbuf *packet, const ip_addr_t *, u16_t) {
    uint8_t message[548];
    size_t length = pbuf_copy_partial(packet, message, sizeof(message), 0);
    pbuf_free(packet);
    if (length < bootp_bytes + 4 || message[0] != 1 || message[1] != 1 || message[2] != 6 ||
        napt::read32(message + bootp_bytes) != magic_cookie) {
        return;
    }
    const uint8_t *options = message + bootp_bytes + 4;
    size_t options_length = length - bootp_bytes - 4;
    uint8_t option_length = 0;
    const uint8_t *type = find_option(options, options_length, option_message_type, option_length);
    if (!type || option_length != 1) {
        return;
    }
    switch (*type) {
    case discover:
        reply(message, offer, brain_link_client);
        break;
    case request_message: {
        uint32_t requested = napt::read32(message + 12);
        const uint8_t *requested_option = find_option(options, options_length, option_requested_address, option_length);
        if (requested_option && option_length == 4) {
            requested = napt::read32(requested_option);
        }
        if (requested != brain_link_client) {
            log_line("dhcp: refused request for %08lx", static_cast<unsigned long>(requested));
            reply(message, refuse, 0);
            break;
        }
        if (leased != brain_link_client) {
            log_line("dhcp: leased 10.88.0.2 to the Brain");
        }
        leased = brain_link_client;
        reply(message, acknowledge, brain_link_client);
        break;
    }
    case inform:
        reply(message, acknowledge, 0);
        break;
    default:
        break;
    }
}

}

void dhcp_server_init() {
    server = udp_new();
    udp_bind(server, IP4_ADDR_ANY, server_port);
    udp_bind_netif(server, brain_link_netif());
    udp_recv(server, received, nullptr);
}

uint32_t dhcp_server_leased_address() {
    return leased;
}
