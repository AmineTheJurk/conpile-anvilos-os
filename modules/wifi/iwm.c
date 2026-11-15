#include "kmod.h"

const mod_meta_raw_t __attribute__((section(".modmeta"), used)) mod_meta = {
    .drv_class   = 3,
    .match_class = 0xFF,
    .match_sub   = 0xFF,
    .name        = "iwm",
    .origin      = "openbsd:sys/dev/pci/if_iwm.c"
};

const mod_pci_id_t __attribute__((section(".modids"), used)) mod_pci_ids[] = {
    { 0x8086, 0x08B3 },
    { 0x8086, 0x08B4 },
    { 0x8086, 0x3165 },
    { 0x8086, 0x3166 },
    { 0x8086, 0x24FB },
    { 0x8086, 0x08B1 },
    { 0x8086, 0x08B2 },
    { 0x8086, 0x095A },
    { 0x8086, 0x095B },
    { 0x8086, 0x24F3 },
    { 0x8086, 0x24F4 },
    { 0x8086, 0x24FD },
    { 0x8086, 0x2526 },
    { 0x8086, 0x9DF0 },
    { 0x8086, 0xA370 },
    { 0x8086, 0x31DC }
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
    api->register_net("iwm", true, mac, true, 866, base);
    return 0;
}
