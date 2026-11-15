#include "kmod.h"

const mod_meta_raw_t __attribute__((section(".modmeta"), used)) mod_meta = {
    .drv_class   = 4,
    .match_class = 0xFF,
    .match_sub   = 0xFF,
    .name        = "re",
    .origin      = "openbsd:sys/dev/pci/if_re_pci.c"
};

const mod_pci_id_t __attribute__((section(".modids"), used)) mod_pci_ids[] = {
    { 0x1259, 0xC107 },
    { 0x1186, 0x4300 },
    { 0x1186, 0x4302 },
    { 0x10EC, 0x2502 },
    { 0x10EC, 0x2600 },
    { 0x10EC, 0x8136 },
    { 0x10EC, 0x8168 },
    { 0x10EC, 0x8161 },
    { 0x10EC, 0x8169 },
    { 0x10EC, 0x8167 },
    { 0x0357, 0x000A },
    { 0x16EC, 0x0116 },
    { 0x10EC, 0x8139 },
    { 0x10EC, 0x1032 }
};

int mod_init(const kmod_api_t* api, const kmod_pci_dev_t* dev) {
    api->pci_enable_busmaster(dev->bus, dev->slot, dev->func);
    uint32_t io_base = dev->bar[0] & ~0x3U;
    uint32_t mmio_phys = dev->bar[2] & ~0xFU;
    uint8_t mac[6] = {0};
    if (dev->bar[0] & 1) {
        for (int i = 0; i < 6; i++) mac[i] = api->inb((uint16_t)(io_base + i));
    } else if (mmio_phys) {
        volatile uint8_t* mm = (volatile uint8_t*)api->vmm_map_mmio(mmio_phys, 0x1000);
        if (mm) for (int i = 0; i < 6; i++) mac[i] = mm[i];
    }
    api->register_net("re", false, mac, true, 1000, mmio_phys ? mmio_phys : io_base);
    return 0;
}
