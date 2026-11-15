#include "kmod.h"

const mod_meta_raw_t __attribute__((section(".modmeta"), used)) mod_meta = {
    .drv_class   = 4,
    .match_class = 0xFF,
    .match_sub   = 0xFF,
    .name        = "bnxt",
    .origin      = "openbsd:sys/dev/pci/if_bnxt.c"
};

const mod_pci_id_t __attribute__((section(".modids"), used)) mod_pci_ids[] = {
    { 0x14E4, 0x16C8 },
    { 0x14E4, 0x16C9 },
    { 0x14E4, 0x16CA },
    { 0x14E4, 0x16CE },
    { 0x14E4, 0x16CF },
    { 0x14E4, 0x16DF },
    { 0x14E4, 0x16D0 },
    { 0x14E4, 0x16D1 },
    { 0x14E4, 0x16D2 },
    { 0x14E4, 0x16D5 },
    { 0x14E4, 0x16D6 },
    { 0x14E4, 0x16D7 },
    { 0x14E4, 0x16D8 },
    { 0x14E4, 0x16E3 },
    { 0x14E4, 0x16D9 },
    { 0x14E4, 0x16E2 },
    { 0x14E4, 0x16F1 },
    { 0x14E4, 0x1614 },
    { 0x14E4, 0x1752 },
    { 0x14E4, 0x1751 },
    { 0x14E4, 0x1750 }
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
    api->register_net("bnxt", false, mac, true, 1000, base);
    return 0;
}
