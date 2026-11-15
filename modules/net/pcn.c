#include "kmod.h"

const mod_meta_raw_t __attribute__((section(".modmeta"), used)) mod_meta = {
    .drv_class   = 4,
    .match_class = 0xFF,
    .match_sub   = 0xFF,
    .name        = "pcn",
    .origin      = "openbsd:sys/dev/pci/if_pcn.c"
};

const mod_pci_id_t __attribute__((section(".modids"), used)) mod_pci_ids[] = {
    { 0x1022, 0x2000 },
    { 0x1022, 0x2001 },
    { 0x1023, 0x2000 }
};

int mod_init(const kmod_api_t* api, const kmod_pci_dev_t* dev) {
    api->pci_enable_busmaster(dev->bus, dev->slot, dev->func);
    uint32_t io_base = dev->bar[0] & ~0x3U;
    uint8_t mac[6] = {0};
    if (dev->bar[0] & 1) {
        for (int i = 0; i < 6; i++) mac[i] = api->inb((uint16_t)(io_base + i));
    }
    api->register_net("pcn", false, mac, true, 100, io_base);
    return 0;
}
