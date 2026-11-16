#include "kmod.h"

const mod_meta_raw_t __attribute__((section(".modmeta"), used)) mod_meta = {
    .drv_class   = 4,
    .match_class = 0xFF,
    .match_sub   = 0xFF,
    .name        = "aq",
    .origin      = "openbsd:sys/dev/pci/if_aq.c"
};

const mod_pci_id_t __attribute__((section(".modids"), used)) mod_pci_ids[] = {
    { 0x1D6A, 0x0001 },
    { 0x1D6A, 0x0002 },
    { 0x1D6A, 0x0003 },
    { 0x1D6A, 0x0004 },
    { 0x1D6A, 0x0005 },
    { 0x1D6A, 0x0006 },
    { 0x1D6A, 0x0007 },
    { 0x1D6A, 0x0008 }
};

int mod_init(const kmod_api_t* api, const kmod_pci_dev_t* dev) {
    api->pci_enable_busmaster(dev->bus, dev->slot, dev->func);
    uint32_t base = (dev->bar[0] & 1) ? (dev->bar[0] & ~0x3U) : (dev->bar[0] & ~0xFU);
    if (base && !(dev->bar[0] & 1)) api->vmm_map_mmio(base, 0x4000);

    uint8_t mac[6] = { 0 };
    mac[0] = 0x02;
    mac[1] = (uint8_t)(dev->vendor_id >> 8);
    mac[2] = (uint8_t)(dev->vendor_id & 0xFF);
    mac[3] = (uint8_t)(dev->device_id >> 8);
    mac[4] = (uint8_t)(dev->device_id & 0xFF);
    mac[5] = (uint8_t)((dev->bus << 4) | dev->slot);

    api->register_net("aq", false, mac, true, 1000, base);
    return 0;
}
