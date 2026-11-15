#include "kmod.h"

const mod_meta_raw_t __attribute__((section(".modmeta"), used)) mod_meta = {
    .drv_class   = 4,
    .match_class = 0xFF,
    .match_sub   = 0xFF,
    .name        = "igc",
    .origin      = "openbsd:sys/dev/pci/if_igc.c"
};

const mod_pci_id_t __attribute__((section(".modids"), used)) mod_pci_ids[] = {
    { 0x8086, 0x15F7 },
    { 0x8086, 0x125E },
    { 0x8086, 0x15FD },
    { 0x8086, 0x15F8 },
    { 0x8086, 0x0D9F },
    { 0x8086, 0x3100 },
    { 0x8086, 0x3101 },
    { 0x8086, 0x15F2 },
    { 0x8086, 0x5502 },
    { 0x8086, 0x15F3 },
    { 0x8086, 0x125F },
    { 0x8086, 0x125D },
    { 0x8086, 0x125B },
    { 0x8086, 0x5503 },
    { 0x8086, 0x3102 },
    { 0x8086, 0x125C }
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
    api->register_net("igc", false, mac, true, 1000, base);
    return 0;
}
