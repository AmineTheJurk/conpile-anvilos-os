#include "kmod.h"

const mod_meta_raw_t __attribute__((section(".modmeta"), used)) mod_meta = {
    .drv_class   = 4,
    .match_class = 0xFF,
    .match_sub   = 0xFF,
    .name        = "tl",
    .origin      = "openbsd:sys/dev/pci/if_tl.c"
};

const mod_pci_id_t __attribute__((section(".modids"), used)) mod_pci_ids[] = {
    { 0x0E11, 0xAE32 },
    { 0x0E11, 0xAE34 },
    { 0x0E11, 0xAE35 },
    { 0x0E11, 0xAE43 },
    { 0x0E11, 0xAE40 },
    { 0x0E11, 0xB011 },
    { 0x0E11, 0xF150 },
    { 0x0E11, 0xF130 },
    { 0x104C, 0x0500 },
    { 0x104C, 0xAE32 },
    { 0x104C, 0xAE34 },
    { 0x104C, 0xAE35 },
    { 0x104C, 0xAE40 },
    { 0x104C, 0xAE43 },
    { 0x104C, 0xB011 },
    { 0x104C, 0xB012 },
    { 0x104C, 0xB030 },
    { 0x104C, 0xF130 },
    { 0x104C, 0xF150 },
    { 0x104C, 0x0013 },
    { 0x104C, 0x0012 },
    { 0x104C, 0x0014 }
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
    api->register_net("tl", false, mac, true, 1000, base);
    return 0;
}
