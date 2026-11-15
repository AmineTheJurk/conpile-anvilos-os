#include "kmod.h"

const mod_meta_raw_t __attribute__((section(".modmeta"), used)) mod_meta = {
    .drv_class   = 1,
    .match_class = 0xFF,
    .match_sub   = 0xFF,
    .name        = "viogpu",
    .origin      = "openbsd:sys/dev/pv/viogpu.c"
};

const mod_pci_id_t __attribute__((section(".modids"), used)) mod_pci_ids[] = {
    { 0x1AF4, 0x1050 }
};

int mod_init(const kmod_api_t* api, const kmod_pci_dev_t* dev) {
    api->pci_enable_busmaster(dev->bus, dev->slot, dev->func);
    uint32_t mmio_phys = dev->bar[0] & ~0xFU;
    if (mmio_phys) api->vmm_map_mmio(mmio_phys, 0x4000);
    api->register_gpu("viogpu", "VirtIO GPU", dev->device_id, mmio_phys, 64, "OpenBSD VirtIO GPU");
    return 0;
}
