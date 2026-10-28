// The lwIP network interface on the Brain side of the RNDIS link (10.88.0.1/24).
#pragma once

#include <cstddef>
#include <cstdint>

#include "lwip/ip4_addr.h"

struct netif;

constexpr uint32_t brain_link_network = 0x0a580000;
constexpr uint32_t brain_link_mask = 0xffffff00;
constexpr uint32_t brain_link_router = 0x0a580001;
constexpr uint32_t brain_link_client = 0x0a580002;

void brain_link_init();
netif *brain_link_netif();
void brain_link_set_up(bool up);
bool brain_link_is_up();
void brain_link_input(const uint8_t *frame, size_t length);
ip4_addr_t brain_link_address(uint32_t host_order);
