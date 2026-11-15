#include "kmod.h"

const mod_meta_raw_t __attribute__((section(".modmeta"), used)) mod_meta = {
    .drv_class   = 4,
    .match_class = 0xFF,
    .match_sub   = 0xFF,
    .name        = "msk",
    .origin      = "openbsd:sys/dev/pci/if_msk.c"
};

const mod_pci_id_t __attribute__((section(".modids"), used)) mod_pci_ids[] = {
    { 0x1186, 0x4001 },
    { 0x1186, 0x4B03 },
    { 0x1186, 0x4B02 },
    { 0x1186, 0x4B00 },
    { 0x11AB, 0x4340 },
    { 0x11AB, 0x4344 },
    { 0x11AB, 0x4341 },
    { 0x11AB, 0x4345 },
    { 0x11AB, 0x4350 },
    { 0x11AB, 0x4351 },
    { 0x11AB, 0x4352 },
    { 0x11AB, 0x4353 },
    { 0x11AB, 0x4354 },
    { 0x11AB, 0x4355 },
    { 0x11AB, 0x4357 },
    { 0x11AB, 0x435A },
    { 0x11AB, 0x4361 },
    { 0x11AB, 0x4360 },
    { 0x11AB, 0x4362 },
    { 0x11AB, 0x4363 },
    { 0x11AB, 0x436D },
    { 0x11AB, 0x4364 },
    { 0x11AB, 0x4380 },
    { 0x11AB, 0x436A },
    { 0x11AB, 0x4381 },
    { 0x11AB, 0x4342 },
    { 0x11AB, 0x4346 },
    { 0x11AB, 0x4343 },
    { 0x11AB, 0x4347 },
    { 0x11AB, 0x4365 },
    { 0x11AB, 0x436B },
    { 0x11AB, 0x436C },
    { 0x11AB, 0x4370 },
    { 0x11AB, 0x4382 },
    { 0x11AB, 0x4367 },
    { 0x11AB, 0x4356 },
    { 0x11AB, 0x4368 },
    { 0x11AB, 0x4366 },
    { 0x11AB, 0x4369 },
    { 0x1148, 0x9E00 },
    { 0x1148, 0x9000 }
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
    api->register_net("msk", false, mac, true, 1000, base);
    return 0;
}
