#include "kmod.h"

const mod_meta_raw_t __attribute__((section(".modmeta"), used)) mod_meta = {
    .drv_class   = 3,
    .match_class = 0xFF,
    .match_sub   = 0xFF,
    .name        = "athn",
    .origin      = "openbsd:sys/dev/pci/if_athn_pci.c"
};

const mod_pci_id_t __attribute__((section(".modids"), used)) mod_pci_ids[] = {
    { 0x168C, 0x0023 },
    { 0x168C, 0x0024 },
    { 0x168C, 0x0027 },
    { 0x168C, 0x0029 },
    { 0x168C, 0x002A },
    { 0x168C, 0x002B },
    { 0x168C, 0x002C },
    { 0x168C, 0x002D },
    { 0x168C, 0x002E }
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
    api->register_net("athn", true, mac, true, 866, base);
    return 0;
}
