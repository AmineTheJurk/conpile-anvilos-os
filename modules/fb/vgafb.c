#include "kmod.h"

const mod_meta_raw_t __attribute__((section(".modmeta"), used)) mod_meta = {
    .drv_class   = 2,
    .match_class = 0x03,
    .match_sub   = 0x00,
    .name        = "vgafb",
    .origin      = "openbsd:sys/dev/pci/vga_pci.c"
};

const mod_pci_id_t __attribute__((section(".modids"), used)) mod_pci_ids[] = {
    { 0x1234, 0x1111 },
    { 0x80EE, 0xBEEF },
    { 0x15AD, 0x0405 },
    { 0x1B36, 0x0100 },
    { 0x1013, 0x00B8 },
    { 0x1013, 0x00D0 },
    { 0x102B, 0x0522 },
    { 0x102B, 0x0532 },
    { 0x1A03, 0x2000 }
};

int mod_init(const kmod_api_t* api, const kmod_pci_dev_t* dev) {
    api->pci_enable_busmaster(dev->bus, dev->slot, dev->func);
    uint32_t lfb = (api->boot_info && api->boot_info->vbe_active) ? api->boot_info->fb_addr : (dev->bar[0] & ~0xFU);
    uint32_t w = api->boot_info ? api->boot_info->fb_width : 1024;
    uint32_t h = api->boot_info ? api->boot_info->fb_height : 768;
    api->register_gpu("vgafb", "PCI/Bochs VGA", dev->device_id, lfb, 16, "OpenBSD vga_pci/BGA");
    api->register_fb("vgafb", lfb, w, h, 32);
    return 0;
}
