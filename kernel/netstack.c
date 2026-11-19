#include "netstack.h"
#include "kmod.h"
#include "mem.h"
#include "string.h"
#include "serial.h"

#define ARP_CACHE_SIZE 16
#define ETH_ADDR_LEN 6
#define ARP_PACKET_LEN 28
#define E1000_RING_COUNT 8
#define E1000_BUFFER_SIZE 2048
#define E1000_MAX_ADAPTERS 1

/* Intel 82540/8254x register offsets. */
#define E1000_CTRL   0x0000
#define E1000_STATUS 0x0008
#define E1000_EERD   0x0014
#define E1000_ICR    0x00C0
#define E1000_IMC    0x00D8
#define E1000_RCTL   0x0100
#define E1000_TCTL   0x0400
#define E1000_TIPG   0x0410
#define E1000_RDBAL  0x2800
#define E1000_RDBAH  0x2804
#define E1000_RDLEN  0x2808
#define E1000_RDH    0x2810
#define E1000_RDT    0x2818
#define E1000_TDBAL  0x3800
#define E1000_TDBAH  0x3804
#define E1000_TDLEN  0x3808
#define E1000_TDH    0x3810
#define E1000_TDT    0x3818
#define E1000_RAL0   0x5400
#define E1000_RAH0   0x5404

#define E1000_RXD_DD 0x01
#define E1000_RXD_EOP 0x02
#define E1000_TXD_DD 0x01
#define E1000_TXD_EOP 0x01
#define E1000_TXD_IFCS 0x02
#define E1000_TXD_RS 0x08

/* Legacy 16-byte descriptors, as specified by the 8254x programming manual. */
typedef struct __attribute__((packed, aligned(16))) {
    uint64_t address;
    uint16_t length;
    uint8_t cso;
    uint8_t command;
    uint8_t status;
    uint8_t css;
    uint16_t special;
} e1000_tx_desc_t;

typedef struct __attribute__((packed, aligned(16))) {
    uint64_t address;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} e1000_rx_desc_t;

typedef struct {
    volatile uint32_t* regs;
    e1000_tx_desc_t* tx;
    e1000_rx_desc_t* rx;
    uint8_t* tx_buf[E1000_RING_COUNT];
    uint8_t* rx_buf[E1000_RING_COUNT];
    uint32_t tx_head;
    uint32_t rx_next;
    bool ready;
} e1000_state_t;

static net_interface_t interfaces[NET_MAX_INTERFACES];
static net_ipv4_config_t ipv4_configs[NET_MAX_INTERFACES];
static netstack_tx_fn transmitters[NET_MAX_INTERFACES];
static e1000_state_t e1000_adapters[E1000_MAX_ADAPTERS];
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

static void* alloc_aligned16(uint32_t size) {
    uint8_t* raw = (uint8_t*)kmalloc(size + 15);
    if (!raw) return NULL;
    return (void*)(((uint32_t)raw + 15U) & ~15U);
}

static uint32_t e1000_read(e1000_state_t* s, uint32_t reg) {
    return s->regs[reg / 4];
}
static void e1000_write(e1000_state_t* s, uint32_t reg, uint32_t value) {
    s->regs[reg / 4] = value;
    (void)s->regs[E1000_STATUS / 4]; /* flush posted MMIO write */
}

static bool e1000_transmit(int index, const uint8_t* frame, uint32_t length) {
    if (index < 0 || index >= interface_count || !frame || !length || length > NET_MAX_PACKET) return false;
    e1000_state_t* s = NULL;
    for (int i = 0; i < E1000_MAX_ADAPTERS; i++) {
        if (e1000_adapters[i].ready && interfaces[index].driver[0] &&
            strcmp(interfaces[index].driver, "e1000") == 0) {
            s = &e1000_adapters[i];
            break;
        }
    }
    if (!s) return false;

    uint32_t slot = s->tx_head % E1000_RING_COUNT;
    e1000_tx_desc_t* d = &s->tx[slot];
    if (!(d->status & E1000_TXD_DD)) return false;
    uint32_t wire_len = length < 60 ? 60 : length;
    memcpy(s->tx_buf[slot], frame, length);
    if (wire_len > length) memset(s->tx_buf[slot] + length, 0, wire_len - length);
    d->length = (uint16_t)wire_len;
    d->command = E1000_TXD_EOP | E1000_TXD_IFCS | E1000_TXD_RS;
    d->status = 0;
    s->tx_head = (slot + 1) % E1000_RING_COUNT;
    e1000_write(s, E1000_TDT, s->tx_head);
    return true;
}

static bool e1000_init(int index, uint32_t mmio_base) {
    if (index < 0 || index >= interface_count || !mmio_base) return false;
    e1000_state_t* s = &e1000_adapters[0];
    memset(s, 0, sizeof(*s));
    s->regs = (volatile uint32_t*)mmio_base;
    s->tx = (e1000_tx_desc_t*)alloc_aligned16(sizeof(e1000_tx_desc_t) * E1000_RING_COUNT);
    s->rx = (e1000_rx_desc_t*)alloc_aligned16(sizeof(e1000_rx_desc_t) * E1000_RING_COUNT);
    if (!s->tx || !s->rx) return false;
    memset(s->tx, 0, sizeof(e1000_tx_desc_t) * E1000_RING_COUNT);
    memset(s->rx, 0, sizeof(e1000_rx_desc_t) * E1000_RING_COUNT);

    for (uint32_t i = 0; i < E1000_RING_COUNT; i++) {
        s->tx_buf[i] = (uint8_t*)alloc_aligned16(E1000_BUFFER_SIZE);
        s->rx_buf[i] = (uint8_t*)alloc_aligned16(E1000_BUFFER_SIZE);
        if (!s->tx_buf[i] || !s->rx_buf[i]) return false;
        s->tx[i].address = (uint32_t)s->tx_buf[i];
        s->tx[i].status = E1000_TXD_DD;
        s->rx[i].address = (uint32_t)s->rx_buf[i];
    }

    /* Disable interrupts and reset before programming legacy descriptor rings. */
    e1000_write(s, E1000_IMC, 0xFFFFFFFFU);
    (void)e1000_read(s, E1000_ICR);
    e1000_write(s, E1000_CTRL, e1000_read(s, E1000_CTRL) | (1U << 26));
    for (volatile uint32_t wait = 0; wait < 100000; wait++) { }

    e1000_write(s, E1000_TDBAL, (uint32_t)s->tx);
    e1000_write(s, E1000_TDBAH, 0);
    e1000_write(s, E1000_TDLEN, sizeof(e1000_tx_desc_t) * E1000_RING_COUNT);
    e1000_write(s, E1000_TDH, 0);
    e1000_write(s, E1000_TDT, 0);

    e1000_write(s, E1000_RDBAL, (uint32_t)s->rx);
    e1000_write(s, E1000_RDBAH, 0);
    e1000_write(s, E1000_RDLEN, sizeof(e1000_rx_desc_t) * E1000_RING_COUNT);
    e1000_write(s, E1000_RDH, 0);
    e1000_write(s, E1000_RDT, E1000_RING_COUNT - 1);

    /* Enable receive, accept broadcast; enable transmit and pad short frames. */
    e1000_write(s, E1000_RCTL, (1U << 1) | (1U << 15) | (1U << 26));
    e1000_write(s, E1000_TCTL, (1U << 1) | (1U << 3) | (0x10U << 4) | (0x40U << 12));
    e1000_write(s, E1000_TIPG, 0x0060200AU);
    s->rx_next = 0;
    s->tx_head = 0;
    s->ready = true;
    interfaces[index].link_up = (e1000_read(s, E1000_STATUS) & 0x02U) != 0;
    transmitters[index] = e1000_transmit;
    serial_printf("[e1000] RX/TX rings initialized at %p/%p, link %s\n",
                  (void*)s->rx, (void*)s->tx, interfaces[index].link_up ? "up" : "down");
    return true;
}

static bool send_frame(int index, const uint8_t* frame, uint32_t length) {
    if (index < 0 || index >= interface_count || !frame ||
        length > NET_MAX_PACKET || !transmitters[index] || !interfaces[index].link_up) return false;
    return transmitters[index](index, frame, length);
}

void netstack_init(void) {
    memset(interfaces, 0, sizeof(interfaces));
    memset(ipv4_configs, 0, sizeof(ipv4_configs));
    memset(transmitters, 0, sizeof(transmitters));
    memset(e1000_adapters, 0, sizeof(e1000_adapters));
    memset(arp_cache, 0, sizeof(arp_cache));
    interface_count = 0;
    arp_next = 0;
    next_ip_id = 1;

    int count = kmod_net_count();
    if (count > NET_MAX_INTERFACES) count = NET_MAX_INTERFACES;
    for (int i = 0; i < count; i++) {
        const net_iface_info_t* ni = kmod_get_net(i);
        if (!ni) continue;
        int idx = interface_count++;
        net_interface_t* iface = &interfaces[idx];
        strncpy(iface->name, ni->iface, sizeof(iface->name) - 1);
        strncpy(iface->driver, ni->driver, sizeof(iface->driver) - 1);
        memcpy(iface->mac, ni->mac, ETH_ADDR_LEN);
        iface->speed_mbps = ni->speed_mbps;
        iface->link_up = ni->link_up;
        iface->is_wifi = ni->is_wifi;
        ipv4_configs[idx].ip = 0;
        ipv4_configs[idx].netmask = 0;
        ipv4_configs[idx].gateway = 0;

        if (strcmp(iface->driver, "e1000") == 0) {
            if (!e1000_init(idx, ni->base_addr)) {
                iface->link_up = false;
                serial_puts("[e1000] Failed to initialize packet rings; interface remains down.\n");
            }
        }
        serial_printf("[NET] Interface %s (%s), %u Mbps, link %s\n",
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
void netstack_set_rx_handler(netstack_rx_fn handler) { receive_handler = handler; }

static void arp_remember(int index, uint32_t ip, const uint8_t* mac) {
    if (index < 0 || index >= interface_count || !mac || !ip) return;
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].iface == index && arp_cache[i].ip == ip) {
            memcpy(arp_cache[i].mac, mac, ETH_ADDR_LEN);
            return;
        }
    }
    arp_entry_t* e = &arp_cache[arp_next++ % ARP_CACHE_SIZE];
    e->valid = true; e->iface = index; e->ip = ip;
    memcpy(e->mac, mac, ETH_ADDR_LEN);
}

static bool arp_lookup(int index, uint32_t ip, uint8_t* mac) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].iface == index && arp_cache[i].ip == ip) {
            memcpy(mac, arp_cache[i].mac, ETH_ADDR_LEN);
            return true;
        }
    }
    return false;
}

static bool send_arp_reply(int index, const uint8_t* target_mac, uint32_t target_ip) {
    uint8_t frame[NET_ETH_HEADER + ARP_PACKET_LEN];
    memset(frame, 0, sizeof(frame));
    memcpy(frame, target_mac, ETH_ADDR_LEN);
    memcpy(frame + 6, interfaces[index].mac, ETH_ADDR_LEN);
    put_be16(frame + 12, NET_ETHERTYPE_ARP);
    uint8_t* arp = frame + NET_ETH_HEADER;
    put_be16(arp, 1); put_be16(arp + 2, NET_ETHERTYPE_IPV4);
    arp[4] = 6; arp[5] = 4; put_be16(arp + 6, 2);
    memcpy(arp + 8, interfaces[index].mac, ETH_ADDR_LEN);
    put_be32(arp + 14, ipv4_configs[index].ip);
    memcpy(arp + 18, target_mac, ETH_ADDR_LEN);
    put_be32(arp + 24, target_ip);
    return send_frame(index, frame, sizeof(frame));
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
        uint32_t target_ip = get_be32(payload + 24);
        arp_remember(index, sender_ip, payload + 8);
        if (get_be16(payload + 6) == 1 && target_ip == ipv4_configs[index].ip && target_ip)
            (void)send_arp_reply(index, payload + 8, sender_ip);
    } else if (type == NET_ETHERTYPE_IPV4 && payload_len >= NET_IPV4_HEADER) {
        uint32_t ihl = (payload[0] & 0x0F) * 4U;
        uint16_t total = get_be16(payload + 2);
        if ((payload[0] >> 4) == 4 && ihl >= NET_IPV4_HEADER && ihl <= payload_len &&
            total >= ihl && total <= payload_len &&
            checksum16(payload, ihl) == 0 &&
            get_be32(payload + 16) == ipv4_configs[index].ip) {
            if (receive_handler) receive_handler(index, NET_ETHERTYPE_IPV4, payload, total);
            return;
        }
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
    put_be16(arp, 1); put_be16(arp + 2, NET_ETHERTYPE_IPV4);
    arp[4] = 6; arp[5] = 4; put_be16(arp + 6, 1);
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
    if (!arp_lookup(index, dest_ip, dest_mac)) {
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
    ip[0] = 0x45; put_be16(ip + 2, (uint16_t)ip_len);
    put_be16(ip + 4, next_ip_id++); put_be16(ip + 6, 0x4000);
    ip[8] = 64; ip[9] = protocol; put_be16(ip + 10, 0);
    put_be32(ip + 12, ipv4_configs[index].ip);
    put_be32(ip + 16, dest_ip);
    put_be16(ip + 10, checksum16(ip, NET_IPV4_HEADER));
    if (payload_len) memcpy(ip + NET_IPV4_HEADER, payload, payload_len);
    return send_frame(index, frame, frame_len);
}

void netstack_poll(void) {
    for (int a = 0; a < E1000_MAX_ADAPTERS; a++) {
        e1000_state_t* s = &e1000_adapters[a];
        if (!s->ready) continue;
        int iface = -1;
        for (int i = 0; i < interface_count; i++)
            if (strcmp(interfaces[i].driver, "e1000") == 0) { iface = i; break; }
        if (iface < 0) continue;
        uint32_t guard = 0;
        while (guard++ < E1000_RING_COUNT) {
            uint32_t slot = s->rx_next % E1000_RING_COUNT;
            e1000_rx_desc_t* d = &s->rx[slot];
            if (!(d->status & E1000_RXD_DD)) break;
            if ((d->status & E1000_RXD_EOP) && d->length >= NET_ETH_HEADER && d->length <= E1000_BUFFER_SIZE)
                netstack_receive(iface, s->rx_buf[slot], d->length);
            d->status = 0;
            d->errors = 0;
            d->length = 0;
            s->rx_next = (slot + 1) % E1000_RING_COUNT;
            e1000_write(s, E1000_RDT, slot);
        }
        interfaces[iface].link_up = (e1000_read(s, E1000_STATUS) & 0x02U) != 0;
    }
}
