#include "kmod.h"

const mod_meta_raw_t __attribute__((section(".modmeta"), used)) mod_meta = {
    .drv_class   = 4,
    .match_class = 0xFF,
    .match_sub   = 0xFF,
    .name        = "ixl",
    .origin      = "openbsd:sys/dev/pci/if_ixl.c"
};

const mod_pci_id_t __attribute__((section(".modids"), used)) mod_pci_ids[] = {
    { 0x8086, 0x1572 },
    { 0x8086, 0x104E },
    { 0x8086, 0x1580 },
    { 0x8086, 0x1581 },
    { 0x8086, 0x1583 },
    { 0x8086, 0x1584 },
    { 0x8086, 0x1585 },
    { 0x8086, 0x1586 },
    { 0x8086, 0x1587 },
    { 0x8086, 0x1588 },
    { 0x8086, 0x1589 },
    { 0x8086, 0x158A },
    { 0x8086, 0x158B },
    { 0x8086, 0x15FF },
    { 0x8086, 0x37CE },
    { 0x8086, 0x37CF },
    { 0x8086, 0x37D0 },
    { 0x8086, 0x37D1 },
    { 0x8086, 0x37D2 },
    { 0x8086, 0x37D3 }
};

int mod_init(const kmod_api_t* api, const kmod_pci_dev_t* dev) {
    api->pci_enable_busmaster(dev->bus, dev->slot, dev->func);
    uint32_t base = (dev->bar[0] & 1) ? (dev->bar[0] & ~0x3U) : (dev->bar[0] & ~0xFU);
    if (base && !(dev->bar[0] & 1)) api->vmm_map_mmio(base, 0x4000);
    uint8_t mac[6];
    mac[0] = 0x02;
    mac[1] = (uint8_t)(dev->vendor_id >> 8);
    mac[2] = (uint8_t)(dev->vendor_id & 0xFF);
    mac[3] = (uint8_t)(dev->device_id >> 8);
    mac[4] = (uint8_t)(dev->device_id & 0xFF);
    mac[5] = (uint8_t)((dev->bus << 4) | dev->slot);
    api->register_net("ixl", false, mac, true, 1000, base);
    return 0;
}
