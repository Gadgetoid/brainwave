// Declaration of the lwIP IPv4 input hook, included by lwIP itself.
#ifndef NAPT_HOOK_H
#define NAPT_HOOK_H

struct pbuf;
struct netif;

#ifdef __cplusplus
extern "C" {
#endif

int napt_ip4_input_hook(struct pbuf *packet, struct netif *input_netif);

#ifdef __cplusplus
}
#endif

#endif
