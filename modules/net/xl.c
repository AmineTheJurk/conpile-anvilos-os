#include "kmod.h"

const mod_meta_raw_t __attribute__((section(".modmeta"), used)) mod_meta = {
    .drv_class   = 4,
    .match_class = 0xFF,
    .match_sub   = 0xFF,
    .name        = "xl",
    .origin      = "openbsd:sys/dev/pci/if_xl_pci.c"
};

const mod_pci_id_t __attribute__((section(".modids"), used)) mod_pci_ids[] = {
    { 0x10B7, 0x7646 },
    { 0x10B7, 0x9000 },
    { 0x10B7, 0x9001 },
    { 0x10B7, 0x9004 },
    { 0x10B7, 0x9005 },
    { 0x10B7, 0x9006 },
    { 0x10B7, 0x900A },
    { 0x10B7, 0x9050 },
    { 0x10B7, 0x9051 },
    { 0x10B7, 0x9055 },
    { 0x10B7, 0x9056 },
    { 0x10B7, 0x9058 },
    { 0x10B7, 0x905A },
    { 0x10B7, 0x9800 },
    { 0x10B7, 0x9805 },
    { 0x10B7, 0x9200 },
    { 0x10B7, 0x4500 },
    { 0x10B7, 0x5055 },
    { 0x10B7, 0x6055 },
    { 0x10B7, 0x6056 },
    { 0x10B7, 0x9201 },
    { 0x10B7, 0x9202 },
    { 0x10B7, 0x5057 },
    { 0x10B7, 0x5157 },
    { 0x10B7, 0x5257 },
    { 0x10B7, 0x6560 },
    { 0x10B7, 0x6562 },
    { 0x10B7, 0x6564 }
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
    api->register_net("xl", false, mac, true, 1000, base);
    return 0;
}
