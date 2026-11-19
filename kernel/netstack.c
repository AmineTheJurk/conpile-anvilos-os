#include "netstack.h"
#include "kmod.h"
#include "string.h"
#include "serial.h"

#define ARP_CACHE_SIZE 16
#define ETH_ADDR_LEN 6
#define ARP_PACKET_LEN 28

static net_interface_t interfaces[NET_MAX_INTERFACES];
static net_ipv4_config_t ipv4_configs[NET_MAX_INTERFACES];
static netstack_tx_fn transmitters[NET_MAX_INTERFACES];
static int interface_count;
static netstack_rx_fn receive_handler;
static uint16_t next_ip_id = 1;

typedef struct {
    bool valid;
    int iface;
    uint32_t ip;
    uint8_t mac[ETH_ADDR_LEN];
} arp_entry_t;
static arp_entry_t arp_cache[ARP_CACHE_SIZE];
static uint32_t arp_next;

static void put_be16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v;
}
static void put_be32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static uint16_t get_be16(const uint8_t* p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}
static uint32_t get_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static uint16_t checksum16(const uint8_t* data, uint32_t length) {
    uint32_t sum = 0;
    while (length >= 2) {
        sum += get_be16(data);
        data += 2;
        length -= 2;
    }
    if (length) sum += (uint16_t)data[0] << 8;
    while (sum >> 16) sum = (sum & 0xFFFFU) + (sum >> 16);
    return (uint16_t)~sum;
}

static bool send_frame(int index, const uint8_t* frame, uint32_t length) {
    if (index < 0 || index >= interface_count || !frame ||
        length > NET_MAX_PACKET || !transmitters[index] || !interfaces[index].link_up) {
        return false;
    }
    return transmitters[index](index, frame, length);
}

void netstack_init(void) {
    memset(interfaces, 0, sizeof(interfaces));
    memset(ipv4_configs, 0, sizeof(ipv4_configs));
    memset(transmitters, 0, sizeof(transmitters));
    memset(arp_cache, 0, sizeof(arp_cache));
    interface_count = 0;
    arp_next = 0;
    next_ip_id = 1;

    int count = kmod_net_count();
    if (count > NET_MAX_INTERFACES) count = NET_MAX_INTERFACES;
    for (int i = 0; i < count; i++) {
        const net_iface_info_t* ni = kmod_get_net(i);
        if (!ni) continue;
        net_interface_t* iface = &interfaces[interface_count];
        strncpy(iface->name, ni->iface, sizeof(iface->name) - 1);
        strncpy(iface->driver, ni->driver, sizeof(iface->driver) - 1);
        memcpy(iface->mac, ni->mac, ETH_ADDR_LEN);
        iface->speed_mbps = ni->speed_mbps;
        iface->link_up = ni->link_up;
        iface->is_wifi = ni->is_wifi;
        interface_count++;
        serial_printf("[NET] Interface %s (%s), %u Mbps, link %s; packet TX not bound\n",
                      iface->name, iface->driver, iface->speed_mbps,
                      iface->link_up ? "up" : "down");
    }
}

int netstack_interface_count(void) { return interface_count; }

const net_interface_t* netstack_get_interface(int index) {
    return (index >= 0 && index < interface_count) ? &interfaces[index] : NULL;
}

void netstack_set_ipv4(int index, uint32_t ip, uint32_t netmask, uint32_t gateway) {
    if (index < 0 || index >= interface_count) return;
    ipv4_configs[index].ip = ip;
    ipv4_configs[index].netmask = netmask;
    ipv4_configs[index].gateway = gateway;
}

bool netstack_get_ipv4(int index, net_ipv4_config_t* out) {
    if (index < 0 || index >= interface_count || !out) return false;
    *out = ipv4_configs[index];
    return true;
}

bool netstack_register_tx(int index, netstack_tx_fn transmit) {
    if (index < 0 || index >= interface_count || !transmit) return false;
    transmitters[index] = transmit;
    return true;
}

void netstack_set_rx_handler(netstack_rx_fn handler) {
    receive_handler = handler;
}

static void arp_remember(int index, uint32_t ip, const uint8_t* mac) {
    if (index < 0 || index >= interface_count || !mac || !ip) return;
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].iface == index && arp_cache[i].ip == ip) {
            memcpy(arp_cache[i].mac, mac, ETH_ADDR_LEN);
            return;
        }
    }
    arp_entry_t* e = &arp_cache[arp_next++ % ARP_CACHE_SIZE];
    e->valid = true;
    e->iface = index;
    e->ip = ip;
    memcpy(e->mac, mac, ETH_ADDR_LEN);
}

void netstack_receive(int index, const uint8_t* frame, uint32_t length) {
    if (index < 0 || index >= interface_count || !frame || length < NET_ETH_HEADER) return;
    uint16_t type = get_be16(frame + 12);
    const uint8_t* payload = frame + NET_ETH_HEADER;
    uint32_t payload_len = length - NET_ETH_HEADER;

    if (type == NET_ETHERTYPE_ARP && payload_len >= ARP_PACKET_LEN &&
        get_be16(payload) == 1 && get_be16(payload + 2) == NET_ETHERTYPE_IPV4 &&
        payload[4] == 6 && payload[5] == 4) {
        uint32_t sender_ip = get_be32(payload + 14);
        arp_remember(index, sender_ip, payload + 8);
    }
    if (receive_handler) receive_handler(index, type, payload, payload_len);
}

bool netstack_send_arp_request(int index, uint32_t target_ip) {
    if (index < 0 || index >= interface_count || !target_ip) return false;
    uint8_t frame[NET_ETH_HEADER + ARP_PACKET_LEN];
    memset(frame, 0, sizeof(frame));
    memset(frame, 0xFF, ETH_ADDR_LEN);
    memcpy(frame + 6, interfaces[index].mac, ETH_ADDR_LEN);
    put_be16(frame + 12, NET_ETHERTYPE_ARP);
    uint8_t* arp = frame + NET_ETH_HEADER;
    put_be16(arp, 1);                    /* Ethernet */
    put_be16(arp + 2, NET_ETHERTYPE_IPV4);
    arp[4] = 6; arp[5] = 4;
    put_be16(arp + 6, 1);                /* request */
    memcpy(arp + 8, interfaces[index].mac, ETH_ADDR_LEN);
    put_be32(arp + 14, ipv4_configs[index].ip);
    memset(arp + 18, 0, ETH_ADDR_LEN);
    put_be32(arp + 24, target_ip);
    return send_frame(index, frame, sizeof(frame));
}

bool netstack_send_ipv4(int index, uint32_t dest_ip, uint8_t protocol,
                        const uint8_t* payload, uint32_t payload_len) {
    if (index < 0 || index >= interface_count || !dest_ip ||
        (!payload && payload_len) || payload_len > NET_MAX_PACKET - NET_ETH_HEADER - NET_IPV4_HEADER ||
        !ipv4_configs[index].ip) return false;

    uint8_t dest_mac[ETH_ADDR_LEN];
    bool resolved = false;
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].iface == index && arp_cache[i].ip == dest_ip) {
            memcpy(dest_mac, arp_cache[i].mac, ETH_ADDR_LEN);
            resolved = true;
            break;
        }
    }
    if (!resolved) {
        (void)netstack_send_arp_request(index, dest_ip);
        return false;
    }

    uint8_t frame[NET_MAX_PACKET];
    uint32_t ip_len = NET_IPV4_HEADER + payload_len;
    uint32_t frame_len = NET_ETH_HEADER + ip_len;
    memset(frame, 0, NET_ETH_HEADER + NET_IPV4_HEADER);
    memcpy(frame, dest_mac, ETH_ADDR_LEN);
    memcpy(frame + 6, interfaces[index].mac, ETH_ADDR_LEN);
    put_be16(frame + 12, NET_ETHERTYPE_IPV4);

    uint8_t* ip = frame + NET_ETH_HEADER;
    ip[0] = 0x45;
    ip[1] = 0;
    put_be16(ip + 2, (uint16_t)ip_len);
    put_be16(ip + 4, next_ip_id++);
    put_be16(ip + 6, 0x4000); /* Don't fragment */
    ip[8] = 64;
    ip[9] = protocol;
    put_be16(ip + 10, 0);
    put_be32(ip + 12, ipv4_configs[index].ip);
    put_be32(ip + 16, dest_ip);
    put_be16(ip + 10, checksum16(ip, NET_IPV4_HEADER));
    if (payload_len) memcpy(ip + NET_IPV4_HEADER, payload, payload_len);
    return send_frame(index, frame, frame_len);
}

void netstack_poll(void) {
    /* Drivers will call netstack_receive() as RX descriptors complete. */
}
