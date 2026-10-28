// The lwIP IPv4 input hook that forwards packets between the Brain link and WiFi, plus the Debug Agent forward.
#include "napt_lwip.h"

#include "lwip/ip4.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "pico/cyw43_arch.h"
#include "pico/time.h"

#include "brain_link.h"
#include "napt.h"
#include "napt_hook.h"

namespace {

napt::Translator translator;
bool configured = false;
bool debug_forward = false;
constexpr uint8_t protocol_tcp = 6;
constexpr uint16_t debug_agent_port = 2100;

netif *wifi_netif() {
    return &cyw43_state.netif[CYW43_ITF_STA];
}

uint32_t now_ms() {
    return to_ms_since_boot(get_absolute_time());
}

bool trim_to_ip_length(pbuf *packet) {
    if (packet->len != packet->tot_len || packet->len < 20) {
        return false;
    }
    const uint8_t *bytes = static_cast<const uint8_t *>(packet->payload);
    uint16_t total = napt::read16(bytes + 2);
    if (total < 20 || total > packet->tot_len) {
        return false;
    }
    if (total < packet->tot_len) {
        pbuf_realloc(packet, total);
    }
    return true;
}

void forward(pbuf *packet, netif *out, uint32_t destination) {
    ip4_addr_t next_hop;
    ip4_addr_set_u32(&next_hop, lwip_htonl(destination));
    out->output(out, packet, &next_hop);
}

}

void napt_init() {
    translator.configure_inside(brain_link_network, brain_link_mask);
    configured = true;
}

void napt_set_debug_forward(bool enabled) {
    debug_forward = enabled;
    if (enabled) {
        translator.add_forward(protocol_tcp, brain_link_client, debug_agent_port);
    } else {
        translator.remove_forward(protocol_tcp, debug_agent_port);
    }
}

bool napt_debug_forward() {
    return debug_forward;
}

void napt_set_outside_address(uint32_t host_order) {
    translator.set_outside_address(host_order);
}

void napt_expire() {
    translator.expire(now_ms());
}

size_t napt_active_mappings() {
    return translator.active_mappings();
}

extern "C" int napt_ip4_input_hook(pbuf *packet, netif *input) {
    if (!configured) {
        return 0;
    }
    netif *link = brain_link_netif();
    netif *wifi = wifi_netif();
    if (input != link && input != wifi) {
        return 0;
    }
    if (!trim_to_ip_length(packet)) {
        return 0;
    }
    uint8_t *bytes = static_cast<uint8_t *>(packet->payload);
    napt::Verdict verdict;
    if (input == link) {
        verdict = translator.outbound(bytes, packet->len, now_ms());
        if (verdict == napt::Verdict::forward && !netif_is_link_up(wifi)) {
            verdict = napt::Verdict::drop;
        }
        if (verdict == napt::Verdict::forward) {
            forward(packet, wifi, napt::read32(bytes + 16));
        }
    } else {
        verdict = translator.inbound(bytes, packet->len, now_ms());
        if (verdict == napt::Verdict::forward && !netif_is_link_up(link)) {
            verdict = napt::Verdict::drop;
        }
        if (verdict == napt::Verdict::forward) {
            forward(packet, link, napt::read32(bytes + 16));
        }
    }
    if (verdict == napt::Verdict::local) {
        return 0;
    }
    pbuf_free(packet);
    return 1;
}
