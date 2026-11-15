#include "kmod.h"

const mod_meta_raw_t __attribute__((section(".modmeta"), used)) mod_meta = {
    .drv_class   = 3,
    .match_class = 0xFF,
    .match_sub   = 0xFF,
    .name        = "ral",
    .origin      = "openbsd:sys/dev/pci/if_ral_pci.c"
};

const mod_pci_id_t __attribute__((section(".modids"), used)) mod_pci_ids[] = {
    { 0x1814, 0x0201 },
    { 0x1814, 0x0302 },
    { 0x1814, 0x0301 },
    { 0x1814, 0x0401 },
    { 0x1814, 0x0601 },
    { 0x1814, 0x0681 },
    { 0x1814, 0x0701 },
    { 0x1814, 0x0781 },
    { 0x1A3B, 0x1059 },
    { 0x1432, 0x7708 },
    { 0x1432, 0x7728 },
    { 0x1432, 0x7758 },
    { 0x1432, 0x7727 },
    { 0x1432, 0x7738 },
    { 0x1432, 0x7748 },
    { 0x1432, 0x7768 },
    { 0x1814, 0x3060 },
    { 0x1814, 0x3062 },
    { 0x1814, 0x3090 },
    { 0x1814, 0x3091 },
    { 0x1814, 0x3092 },
    { 0x1814, 0x3290 },
    { 0x1814, 0x3562 },
    { 0x1814, 0x3592 },
    { 0x1814, 0x3593 },
    { 0x1814, 0x5360 },
    { 0x1814, 0x5390 },
    { 0x1814, 0x5392 },
    { 0x1814, 0x539A },
    { 0x1814, 0x539B },
    { 0x1814, 0x539F }
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
    api->register_net("ral", true, mac, true, 866, base);
    return 0;
}
