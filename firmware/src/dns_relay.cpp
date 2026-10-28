// Forwards DNS queries upstream with rewritten IDs and returns the answers to the Brain.
#include "dns_relay.h"

#include "lwip/dns.h"
#include "lwip/udp.h"
#include "pico/time.h"

#include "brain_link.h"
#include "napt.h"

namespace {

constexpr uint16_t dns_port = 53;
constexpr size_t pending_limit = 16;
constexpr uint32_t pending_timeout_ms = 5000;

struct Pending {
    bool used;
    uint16_t relay_id;
    uint16_t client_id;
    uint16_t client_port;
    ip_addr_t client_address;
    uint32_t started_ms;
};

udp_pcb *listener = nullptr;
udp_pcb *upstream = nullptr;
Pending pending[pending_limit];
uint16_t next_id = 1;

uint32_t now_ms() {
    return to_ms_since_boot(get_absolute_time());
}

void set_id(pbuf *packet, uint16_t id) {
    uint8_t bytes[2];
    napt::write16(bytes, id);
    pbuf_take_at(packet, bytes, 2, 0);
}

uint16_t get_id(pbuf *packet) {
    uint8_t bytes[2] = {};
    pbuf_copy_partial(packet, bytes, 2, 0);
    return napt::read16(bytes);
}

void from_client(void *, udp_pcb *, pbuf *packet, const ip_addr_t *address, u16_t port) {
    const ip_addr_t *server = dns_getserver(0);
    if (packet->tot_len < 12 || !server || ip_addr_isany(server)) {
        pbuf_free(packet);
        return;
    }
    Pending *slot = nullptr;
    for (Pending &entry : pending) {
        if (!entry.used || now_ms() - entry.started_ms > pending_timeout_ms) {
            slot = &entry;
            break;
        }
    }
    if (!slot) {
        pbuf_free(packet);
        return;
    }
    *slot = Pending{true, next_id++, get_id(packet), port, *address, now_ms()};
    set_id(packet, slot->relay_id);
    udp_sendto(upstream, packet, server, dns_port);
    pbuf_free(packet);
}

void from_upstream(void *, udp_pcb *, pbuf *packet, const ip_addr_t *, u16_t) {
    if (packet->tot_len < 12) {
        pbuf_free(packet);
        return;
    }
    uint16_t id = get_id(packet);
    for (Pending &entry : pending) {
        if (entry.used && entry.relay_id == id) {
            entry.used = false;
            set_id(packet, entry.client_id);
            udp_sendto_if(listener, packet, &entry.client_address, entry.client_port, brain_link_netif());
            break;
        }
    }
    pbuf_free(packet);
}

}

void dns_relay_init() {
    listener = udp_new();
    ip_addr_t address = brain_link_address(brain_link_router);
    udp_bind(listener, &address, dns_port);
    udp_recv(listener, from_client, nullptr);
    upstream = udp_new();
    udp_bind(upstream, IP_ADDR_ANY, 0);
    udp_recv(upstream, from_upstream, nullptr);
}

void dns_relay_expire() {
    for (Pending &entry : pending) {
        if (entry.used && now_ms() - entry.started_ms > pending_timeout_ms) {
            entry.used = false;
        }
    }
}
