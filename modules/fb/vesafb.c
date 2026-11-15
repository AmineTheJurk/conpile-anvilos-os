#include "kmod.h"

const mod_meta_raw_t __attribute__((section(".modmeta"), used)) mod_meta = {
    .drv_class   = 2,
    .match_class = 0xFF,
    .match_sub   = 0xFF,
    .name        = "vesafb",
    .origin      = "openbsd:sys/dev/vesa/vesafb.c"
};

const mod_pci_id_t __attribute__((section(".modids"), used)) mod_pci_ids[] = {
    { 0xFFFF, 0xFFFE }
};

int mod_init(const kmod_api_t* api, const kmod_pci_dev_t* dev) {
    (void)dev;
    if (!api->boot_info || !api->boot_info->vbe_active) return -1;
    api->register_fb("vesafb", api->boot_info->fb_addr, api->boot_info->fb_width,
                     api->boot_info->fb_height, api->boot_info->fb_bpp);
    return 0;
}
