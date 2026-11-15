#include "kmod.h"

const mod_meta_raw_t __attribute__((section(".modmeta"), used)) mod_meta = {
    .drv_class   = 4,
    .match_class = 0xFF,
    .match_sub   = 0xFF,
    .name        = "nfe",
    .origin      = "openbsd:sys/dev/pci/if_nfe.c"
};

const mod_pci_id_t __attribute__((section(".modids"), used)) mod_pci_ids[] = {
    { 0x10DE, 0x01C3 },
    { 0x10DE, 0x0066 },
    { 0x10DE, 0x00D6 },
    { 0x10DE, 0x0086 },
    { 0x10DE, 0x008C },
    { 0x10DE, 0x00DF },
    { 0x10DE, 0x00E6 },
    { 0x10DE, 0x0056 },
    { 0x10DE, 0x0057 },
    { 0x10DE, 0x0037 },
    { 0x10DE, 0x0038 },
    { 0x10DE, 0x0268 },
    { 0x10DE, 0x0269 },
    { 0x10DE, 0x0372 },
    { 0x10DE, 0x0373 },
    { 0x10DE, 0x03E5 },
    { 0x10DE, 0x03E6 },
    { 0x10DE, 0x03EE },
    { 0x10DE, 0x03EF },
    { 0x10DE, 0x0450 },
    { 0x10DE, 0x0451 },
    { 0x10DE, 0x0452 },
    { 0x10DE, 0x0453 },
    { 0x10DE, 0x054C },
    { 0x10DE, 0x054D },
    { 0x10DE, 0x054E },
    { 0x10DE, 0x054F },
    { 0x10DE, 0x07DC },
    { 0x10DE, 0x07DD },
    { 0x10DE, 0x07DE },
    { 0x10DE, 0x07DF },
    { 0x10DE, 0x0760 },
    { 0x10DE, 0x0761 },
    { 0x10DE, 0x0762 },
    { 0x10DE, 0x0763 },
    { 0x10DE, 0x0AB0 },
    { 0x10DE, 0x0AB1 },
    { 0x10DE, 0x0AB2 },
    { 0x10DE, 0x0AB3 },
    { 0x10DE, 0x0D7D }
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
    api->register_net("nfe", false, mac, true, 1000, base);
    return 0;
}
