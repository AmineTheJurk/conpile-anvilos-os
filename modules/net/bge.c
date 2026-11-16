#include "kmod.h"

const mod_meta_raw_t __attribute__((section(".modmeta"), used)) mod_meta = {
    .drv_class   = 4,
    .match_class = 0xFF,
    .match_sub   = 0xFF,
    .name        = "bge",
    .origin      = "openbsd:sys/dev/pci/if_bge.c"
};

const mod_pci_id_t __attribute__((section(".modids"), used)) mod_pci_ids[] = {
    { 0x14E4, 0x1644 },
    { 0x14E4, 0x1645 },
    { 0x14E4, 0x1646 },
    { 0x14E4, 0x1647 },
    { 0x14E4, 0x1648 },
    { 0x14E4, 0x1668 },
    { 0x14E4, 0x1669 },
    { 0x14E4, 0x1677 },
    { 0x14E4, 0x1678 },
    { 0x14E4, 0x1679 },
    { 0x14E4, 0x167A },
    { 0x14E4, 0x167B },
    { 0x14E4, 0x167C }
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

    api->register_net("bge", false, mac, true, 1000, base);
    return 0;
}
