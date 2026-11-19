#include "netstack.h"
#include "kmod.h"
#include "string.h"
#include "serial.h"

static net_interface_t interfaces[NET_MAX_INTERFACES];
static net_ipv4_config_t ipv4_configs[NET_MAX_INTERFACES];
static int interface_count;

static uint32_t ipv4_from_bytes(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return (uint32_t)a | ((uint32_t)b << 8) | ((uint32_t)c << 16) | ((uint32_t)d << 24);
}

void netstack_init(void) {
    memset(interfaces, 0, sizeof(interfaces));
    memset(ipv4_configs, 0, sizeof(ipv4_configs));
    interface_count = 0;

    int count = kmod_net_count();
    if (count > NET_MAX_INTERFACES) count = NET_MAX_INTERFACES;
    for (int i = 0; i < count; i++) {
        const net_iface_info_t* ni = kmod_get_net(i);
        net_interface_t* iface = &interfaces[interface_count++];
        strncpy(iface->name, ni->iface, sizeof(iface->name) - 1);
        strncpy(iface->driver, ni->driver, sizeof(iface->driver) - 1);
        memcpy(iface->mac, ni->mac, 6);
        iface->speed_mbps = ni->speed_mbps;
        iface->link_up = ni->link_up;
        iface->is_wifi = ni->is_wifi;

        /* QEMU/user networking defaults; DHCP can replace this later. */
        ipv4_configs[i].ip = ipv4_from_bytes(10, 0, 2, 15);
        ipv4_configs[i].netmask = ipv4_from_bytes(255, 255, 255, 0);
        ipv4_configs[i].gateway = ipv4_from_bytes(10, 0, 2, 2);

        serial_printf("[NET] Interface %s: %02x:%02x:%02x:%02x:%02x:%02x %uMbps %s\n",
                      iface->name,
                      iface->mac[0], iface->mac[1], iface->mac[2],
                      iface->mac[3], iface->mac[4], iface->mac[5],
                      iface->speed_mbps,
                      iface->link_up ? "UP" : "DOWN");
    }
}

int netstack_interface_count(void) { return interface_count; }

const net_interface_t* netstack_get_interface(int index) {
    if (index < 0 || index >= interface_count) return NULL;
    return &interfaces[index];
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

bool netstack_send_arp_request(int index, uint32_t target_ip) {
    (void)index;
    (void)target_ip;
    serial_puts("[NET] ARP transmit requires a device packet-ring implementation.\n");
    return false;
}

bool netstack_send_ipv4(int index, uint32_t dest_ip, uint8_t protocol,
                        const uint8_t* payload, uint32_t payload_len) {
    (void)index;
    (void)dest_ip;
    (void)protocol;
    (void)payload;
    (void)payload_len;
    serial_puts("[NET] IPv4 transmit requires a device packet-ring implementation.\n");
    return false;
}

void netstack_poll(void) {
    /* Packet receive/transmit poll will be driven by completed NIC DMA rings. */
}
