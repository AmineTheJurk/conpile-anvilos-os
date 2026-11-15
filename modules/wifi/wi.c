#include "kmod.h"

const mod_meta_raw_t __attribute__((section(".modmeta"), used)) mod_meta = {
    .drv_class   = 3,
    .match_class = 0xFF,
    .match_sub   = 0xFF,
    .name        = "wi",
    .origin      = "openbsd:sys/dev/pci/if_wi_pci.c"
};

const mod_pci_id_t __attribute__((section(".modids"), used)) mod_pci_ids[] = {
    { 0x16AB, 0x1101 },
    { 0x16AB, 0x1102 },
    { 0x16AB, 0x1100 },
    { 0x16AB, 0x1103 },
    { 0x1638, 0x1100 },
    { 0x16EC, 0x3685 },
    { 0x10B7, 0x7770 },
    { 0x1385, 0x4100 },
    { 0x111A, 0x1023 },
    { 0x4033, 0x7001 },
    { 0xEC80, 0xEC00 },
    { 0x15E8, 0x0130 },
    { 0x15E8, 0x0131 },
    { 0x1260, 0x3873 },
    { 0x1260, 0x3872 },
    { 0x167D, 0xA000 },
    { 0x126C, 0x8030 },
    { 0x1562, 0x0001 }
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
    api->register_net("wi", true, mac, true, 866, base);
    return 0;
}
