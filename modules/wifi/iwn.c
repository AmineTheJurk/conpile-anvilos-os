#include "kmod.h"

const mod_meta_raw_t __attribute__((section(".modmeta"), used)) mod_meta = {
    .drv_class   = 3,
    .match_class = 0xFF,
    .match_sub   = 0xFF,
    .name        = "iwn",
    .origin      = "openbsd:sys/dev/pci/if_iwn.c"
};

const mod_pci_id_t __attribute__((section(".modids"), used)) mod_pci_ids[] = {
    { 0x8086, 0x4229 },
    { 0x8086, 0x4230 },
    { 0x8086, 0x4232 },
    { 0x8086, 0x4237 },
    { 0x8086, 0x423C },
    { 0x8086, 0x423D },
    { 0x8086, 0x4235 },
    { 0x8086, 0x4236 },
    { 0x8086, 0x423A },
    { 0x8086, 0x423B },
    { 0x8086, 0x0083 },
    { 0x8086, 0x0084 },
    { 0x8086, 0x422B },
    { 0x8086, 0x4238 },
    { 0x8086, 0x422C },
    { 0x8086, 0x4239 },
    { 0x8086, 0x0087 },
    { 0x8086, 0x0089 },
    { 0x8086, 0x0082 },
    { 0x8086, 0x0085 },
    { 0x8086, 0x0090 },
    { 0x8086, 0x0091 },
    { 0x8086, 0x008A },
    { 0x8086, 0x008B },
    { 0x8086, 0x08AE },
    { 0x8086, 0x08AF },
    { 0x8086, 0x0896 },
    { 0x8086, 0x0897 },
    { 0x8086, 0x088E },
    { 0x8086, 0x088F },
    { 0x8086, 0x0887 },
    { 0x8086, 0x0888 },
    { 0x8086, 0x0890 },
    { 0x8086, 0x0891 },
    { 0x8086, 0x0892 },
    { 0x8086, 0x0893 },
    { 0x8086, 0x0894 },
    { 0x8086, 0x0895 }
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
    api->register_net("iwn", true, mac, true, 866, base);
    return 0;
}
