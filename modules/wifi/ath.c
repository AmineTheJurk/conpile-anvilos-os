#include "kmod.h"

const mod_meta_raw_t __attribute__((section(".modmeta"), used)) mod_meta = {
    .drv_class   = 3,
    .match_class = 0xFF,
    .match_sub   = 0xFF,
    .name        = "ath",
    .origin      = "openbsd:sys/dev/pci/if_ath_pci.c"
};

const mod_pci_id_t __attribute__((section(".modids"), used)) mod_pci_ids[] = {
    { 0x168C, 0x0007 },
    { 0x168C, 0x0011 },
    { 0x168C, 0x0012 },
    { 0x168C, 0x0013 },
    { 0x168C, 0x0014 },
    { 0x168C, 0x0015 },
    { 0x168C, 0x0016 },
    { 0x168C, 0x0017 },
    { 0x168C, 0x0018 },
    { 0x168C, 0x0019 },
    { 0x168C, 0x001A },
    { 0x168C, 0x001B },
    { 0x168C, 0x001C },
    { 0x168C, 0x001D },
    { 0x168C, 0x0023 },
    { 0x168C, 0x0024 },
    { 0x168C, 0x002C },
    { 0x168C, 0x0207 },
    { 0x168C, 0x1014 },
    { 0x168C, 0x1107 },
    { 0x168C, 0x1112 },
    { 0x168C, 0x1113 },
    { 0x168C, 0xF013 },
    { 0x168C, 0xF11B },
    { 0x168C, 0xFF12 }
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
    api->register_net("ath", true, mac, true, 866, base);
    return 0;
}
