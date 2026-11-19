#ifndef ANVIL_NETSTACK_H
#define ANVIL_NETSTACK_H

#include "types.h"

#define NET_MAX_INTERFACES 16
#define NET_MAX_PACKET 1536
#define NET_ETH_HEADER 14
#define NET_IPV4_HEADER 20
#define NET_ETHERTYPE_IPV4 0x0800
#define NET_ETHERTYPE_ARP  0x0806

/* IPv4 values are stored in network byte order (e.g. 10.0.2.15). */
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

/* A driver supplies transmit and calls netstack_receive() for received frames. */
typedef bool (*netstack_tx_fn)(int interface_index, const uint8_t* frame, uint32_t length);
typedef void (*netstack_rx_fn)(int interface_index, uint16_t ethertype,
                               const uint8_t* payload, uint32_t length);

void netstack_init(void);
int netstack_interface_count(void);
const net_interface_t* netstack_get_interface(int index);
void netstack_set_ipv4(int index, uint32_t ip, uint32_t netmask, uint32_t gateway);
bool netstack_get_ipv4(int index, net_ipv4_config_t* out);
bool netstack_register_tx(int index, netstack_tx_fn transmit);
void netstack_set_rx_handler(netstack_rx_fn handler);
void netstack_receive(int index, const uint8_t* frame, uint32_t length);

bool netstack_send_arp_request(int index, uint32_t target_ip);
bool netstack_send_ipv4(int index, uint32_t dest_ip, uint8_t protocol,
                        const uint8_t* payload, uint32_t payload_len);
void netstack_poll(void);

#endif
