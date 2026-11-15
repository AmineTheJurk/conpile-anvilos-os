#include "kmod.h"

const mod_meta_raw_t __attribute__((section(".modmeta"), used)) mod_meta = {
    .drv_class   = 4,
    .match_class = 0xFF,
    .match_sub   = 0xFF,
    .name        = "ix",
    .origin      = "openbsd:sys/dev/pci/if_ix.c"
};

const mod_pci_id_t __attribute__((section(".modids"), used)) mod_pci_ids[] = {
    { 0x8086, 0x10B6 },
    { 0x8086, 0x1508 },
    { 0x8086, 0x10C6 },
    { 0x8086, 0x10C7 },
    { 0x8086, 0x10C8 },
    { 0x8086, 0x150B },
    { 0x8086, 0x10D7 },
    { 0x8086, 0x10DD },
    { 0x8086, 0x10EC },
    { 0x8086, 0x10F4 },
    { 0x8086, 0x10DB },
    { 0x8086, 0x10E1 },
    { 0x8086, 0x10F1 },
    { 0x8086, 0x10F7 },
    { 0x8086, 0x1514 },
    { 0x8086, 0x10FC },
    { 0x8086, 0x10F8 },
    { 0x8086, 0x152A },
    { 0x8086, 0x10F9 },
    { 0x8086, 0x151C },
    { 0x8086, 0x10FB },
    { 0x8086, 0x1507 },
    { 0x8086, 0x154A },
    { 0x8086, 0x154D },
    { 0x8086, 0x1529 },
    { 0x8086, 0x1557 },
    { 0x8086, 0x1558 },
    { 0x8086, 0x1528 },
    { 0x8086, 0x1560 },
    { 0x8086, 0x1563 },
    { 0x8086, 0x15D1 },
    { 0x8086, 0x15AA },
    { 0x8086, 0x15AB },
    { 0x8086, 0x15AC },
    { 0x8086, 0x15AD },
    { 0x8086, 0x15AE },
    { 0x8086, 0x15C2 },
    { 0x8086, 0x15C3 },
    { 0x8086, 0x15C4 },
    { 0x8086, 0x15CE },
    { 0x8086, 0x15C6 },
    { 0x8086, 0x15C7 },
    { 0x8086, 0x15C8 },
    { 0x8086, 0x15E4 },
    { 0x8086, 0x15E5 },
    { 0x8086, 0x57AF },
    { 0x8086, 0x57B0 },
    { 0x8086, 0x57B1 },
    { 0x8086, 0x57B2 }
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
    api->register_net("ix", false, mac, true, 1000, base);
    return 0;
}
