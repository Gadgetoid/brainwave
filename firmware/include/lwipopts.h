// lwIP configuration: NO_SYS, poll mode, and the IPv4 input hook used for address translation.
#ifndef LWIPOPTS_H
#define LWIPOPTS_H

#define NO_SYS 1
#define LWIP_SOCKET 0
#define LWIP_NETCONN 0
#define MEM_LIBC_MALLOC 0
#define MEM_ALIGNMENT 4
#define MEM_SIZE 32768
#define MEMP_NUM_TCP_SEG 32
#define MEMP_NUM_ARP_QUEUE 10
#define MEMP_NUM_UDP_PCB 8
#define MEMP_NUM_TCP_PCB 6
#define PBUF_POOL_SIZE 32
#define LWIP_ARP 1
#define LWIP_ETHERNET 1
#define LWIP_ICMP 1
#define LWIP_RAW 0
#define TCP_MSS 1460
#define TCP_WND (8 * TCP_MSS)
#define TCP_SND_BUF (4 * TCP_MSS)
#define TCP_SND_QUEUELEN ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))
#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_LINK_CALLBACK 1
#define LWIP_NETIF_HOSTNAME 1
#define LWIP_SINGLE_NETIF 0
#define LWIP_NUM_NETIF_CLIENT_DATA 1
#define LWIP_DHCP 1
#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define LWIP_TCP 1
#define LWIP_UDP 1
#define LWIP_DNS 1
#define LWIP_TCP_KEEPALIVE 1
#define LWIP_NETIF_TX_SINGLE_PBUF 1
#define DHCP_DOES_ARP_CHECK 0
#define LWIP_DHCP_DOES_ACD_CHECK 0
#define IP_FORWARD 0
#define IP_REASSEMBLY 0
#define IP_FRAG 0
#define LWIP_STATS 0
#define LWIP_CHKSUM_ALGORITHM 3

#define LWIP_HOOK_FILENAME "napt_hook.h"
#define LWIP_HOOK_IP4_INPUT(pbuf, input_netif) napt_ip4_input_hook((pbuf), (input_netif))

#endif
