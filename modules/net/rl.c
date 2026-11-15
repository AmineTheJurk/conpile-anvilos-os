#include "kmod.h"

const mod_meta_raw_t __attribute__((section(".modmeta"), used)) mod_meta = {
    .drv_class   = 4,
    .match_class = 0xFF,
    .match_sub   = 0xFF,
    .name        = "rl",
    .origin      = "openbsd:sys/dev/pci/if_rl_pci.c"
};

const mod_pci_id_t __attribute__((section(".modids"), used)) mod_pci_ids[] = {
    { 0x13D1, 0xAB06 },
    { 0x1113, 0x1211 },
    { 0x4033, 0x1360 },
    { 0x1259, 0xA11E },
    { 0x1259, 0xA117 },
    { 0x1500, 0x1360 },
    { 0x1186, 0x4200 },
    { 0x1186, 0x1300 },
    { 0x1186, 0x1340 },
    { 0x00EC, 0x8139 },
    { 0x126C, 0x1211 },
    { 0x14EA, 0xAB06 },
    { 0x14EA, 0xAB07 },
    { 0x10EC, 0x8129 },
    { 0x10EC, 0x8138 },
    { 0x10EC, 0x8039 },
    { 0x10EC, 0x8139 }
};

int mod_init(const kmod_api_t* api, const kmod_pci_dev_t* dev) {
    api->pci_enable_busmaster(dev->bus, dev->slot, dev->func);
    uint32_t io_base = dev->bar[0] & ~0x3U;
    uint8_t mac[6] = {0};
    if (dev->bar[0] & 1) {
        for (int i = 0; i < 6; i++) mac[i] = api->inb((uint16_t)(io_base + i));
    }
    api->register_net("rl", false, mac, true, 100, io_base);
    return 0;
}
