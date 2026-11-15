#include "kmod.h"

const mod_meta_raw_t __attribute__((section(".modmeta"), used)) mod_meta = {
    .drv_class   = 3,
    .match_class = 0xFF,
    .match_sub   = 0xFF,
    .name        = "iwx",
    .origin      = "openbsd:sys/dev/pci/if_iwx.c"
};

const mod_pci_id_t __attribute__((section(".modids"), used)) mod_pci_ids[] = {
    { 0x8086, 0x2723 },
    { 0x8086, 0x02F0 },
    { 0x8086, 0xA0F0 },
    { 0x8086, 0x34F0 },
    { 0x8086, 0x06F0 },
    { 0x8086, 0x43F0 },
    { 0x8086, 0x3DF0 },
    { 0x8086, 0x4DF0 },
    { 0x8086, 0x2725 },
    { 0x8086, 0x2726 },
    { 0x8086, 0x51F0 },
    { 0x8086, 0x7A70 },
    { 0x8086, 0x7AF0 },
    { 0x8086, 0x7E40 },
    { 0x8086, 0x7F70 },
    { 0x8086, 0x54F0 },
    { 0x8086, 0x51F1 },
    { 0x8086, 0x7740 }
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
    api->register_net("iwx", true, mac, true, 866, base);
    return 0;
}
