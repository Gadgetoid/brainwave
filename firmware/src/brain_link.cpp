// An lwIP Ethernet netif whose frames go to and come from the RNDIS host driver.
#include "brain_link.h"

#include <cstring>

#include "lwip/etharp.h"
#include "lwip/netif.h"
#include "netif/ethernet.h"

#include "log.h"
#include "rndis_host.h"

namespace {

netif link;
const uint8_t link_mac[6] = {0x02, 0x62, 0x77, 0x00, 0x00, 0x01};

err_t link_output(netif *, pbuf *frame) {
    return rndis::send_frame(frame) ? ERR_OK : ERR_IF;
}

err_t link_init(netif *interface) {
    interface->name[0] = 'b';
    interface->name[1] = 'r';
    interface->mtu = 1500;
    interface->hwaddr_len = 6;
    memcpy(interface->hwaddr, link_mac, 6);
    interface->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;
    interface->output = etharp_output;
    interface->linkoutput = link_output;
    return ERR_OK;
}

}

ip4_addr_t brain_link_address(uint32_t host_order) {
    ip4_addr_t address;
    ip4_addr_set_u32(&address, lwip_htonl(host_order));
    return address;
}

void brain_link_init() {
    ip4_addr_t address = brain_link_address(brain_link_router);
    ip4_addr_t mask = brain_link_address(brain_link_mask);
    ip4_addr_t gateway = brain_link_address(0);
    netif_add(&link, &address, &mask, &gateway, nullptr, link_init, ethernet_input);
    netif_set_up(&link);
}

netif *brain_link_netif() {
    return &link;
}

void brain_link_set_up(bool up) {
    if (up) {
        netif_set_link_up(&link);
    } else {
        netif_set_link_down(&link);
        etharp_cleanup_netif(&link);
    }
}

bool brain_link_is_up() {
    return netif_is_link_up(&link);
}

void brain_link_input(const uint8_t *frame, size_t length) {
    if (!netif_is_link_up(&link) || length < 14 || length > 1600) {
        return;
    }
    pbuf *packet = pbuf_alloc(PBUF_RAW, static_cast<u16_t>(length), PBUF_POOL);
    if (!packet) {
        return;
    }
    pbuf_take(packet, frame, static_cast<u16_t>(length));
    if (link.input(packet, &link) != ERR_OK) {
        pbuf_free(packet);
    }
}
