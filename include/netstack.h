#ifndef ANVIL_NETSTACK_H
#define ANVIL_NETSTACK_H

#include "types.h"

#define NET_MAX_INTERFACES 16
#define NET_MAX_PACKET 1536

typedef struct {
    uint32_t ip;
    uint32_t netmask;
    uint32_t gateway;
} net_ipv4_config_t;

typedef struct {
    char name[16];
    char driver[24];
    uint8_t mac[6];
    uint32_t speed_mbps;
    bool link_up;
    bool is_wifi;
} net_interface_t;

void netstack_init(void);
int netstack_interface_count(void);
const net_interface_t* netstack_get_interface(int index);
void netstack_set_ipv4(int index, uint32_t ip, uint32_t netmask, uint32_t gateway);
bool netstack_get_ipv4(int index, net_ipv4_config_t* out);

bool netstack_send_arp_request(int index, uint32_t target_ip);
bool netstack_send_ipv4(int index, uint32_t dest_ip, uint8_t protocol,
                        const uint8_t* payload, uint32_t payload_len);
void netstack_poll(void);

#endif
