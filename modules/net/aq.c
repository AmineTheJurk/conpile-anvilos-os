#include "kmod.h"

const mod_meta_raw_t __attribute__((section(".modmeta"), used)) mod_meta = {
    .drv_class   = 4,
    .match_class = 0xFF,
    .match_sub   = 0xFF,
    .name        = "aq",
    .origin      = "openbsd:sys/dev/pci/if_aq_pci.c"
};

const mod_pci_id_t __attribute__((section(".modids"), used)) mod_pci_ids[] = {
    { 0x1D6A, 0x00B1 },
    { 0x1D6A, 0x07B1 },
    { 0x1D6A, 0x08B1 },
    { 0x1D6A, 0x09B1 },
    { 0x1D6A, 0x11B1 },
    { 0x1D6A, 0x12B1 },
    { 0x1D6A, 0x04C0 },
    { 0x1D6A, 0x14C0 },
    { 0x1D6A, 0x34C0 },
    { 0x1D6A, 0x94C0 },
    { 0x1D6A, 0x93C0 },
    { 0x1D6A, 0x12C0 },
    { 0x1D6A, 0x11C0 },
    { 0x1D6A, 0x80B1 },
    { 0x1D6A, 0x87B1 },
    { 0x1D6A, 0x88B1 },
    { 0x1D6A, 0x89B1 },
    { 0x1D6A, 0x91B1 },
    { 0x1D6A, 0x92B1 },
    { 0x1D6A, 0xD100 },
    { 0x1D6A, 0xD107 },
    { 0x1D6A, 0xD108 },
    { 0x1D6A, 0xD109 }
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
    api->register_net("aq", false, mac, true, 1000, base);
    return 0;
}
