#include "kmod.h"

const mod_meta_raw_t __attribute__((section(".modmeta"), used)) mod_meta = {
    .drv_class   = 4,
    .match_class = 0xFF,
    .match_sub   = 0xFF,
    .name        = "fxp",
    .origin      = "openbsd:sys/dev/pci/if_fxp_pci.c"
};

const mod_pci_id_t __attribute__((section(".modids"), used)) mod_pci_ids[] = {
    { 0x8086, 0x1229 },
    { 0x8086, 0x10FE },
    { 0x8086, 0x1030 },
    { 0x8086, 0x1209 },
    { 0x8086, 0x2449 },
    { 0x8086, 0x1035 },
    { 0x8086, 0x1036 },
    { 0x8086, 0x1037 },
    { 0x8086, 0x1031 },
    { 0x8086, 0x1032 },
    { 0x8086, 0x1039 },
    { 0x8086, 0x103D },
    { 0x8086, 0x1050 },
    { 0x8086, 0x1051 },
    { 0x8086, 0x1065 },
    { 0x8086, 0x106A },
    { 0x8086, 0x106B },
    { 0x8086, 0x1033 },
    { 0x8086, 0x1034 },
    { 0x8086, 0x1038 },
    { 0x8086, 0x103B },
    { 0x8086, 0x103C },
    { 0x8086, 0x103E },
    { 0x8086, 0x1052 },
    { 0x8086, 0x1053 },
    { 0x8086, 0x1054 },
    { 0x8086, 0x1055 },
    { 0x8086, 0x1056 },
    { 0x8086, 0x1057 },
    { 0x8086, 0x1058 },
    { 0x8086, 0x1066 },
    { 0x8086, 0x1067 },
    { 0x8086, 0x1091 },
    { 0x8086, 0x1092 },
    { 0x8086, 0x1093 },
    { 0x8086, 0x1094 },
    { 0x8086, 0x1095 },
    { 0x8086, 0x1059 },
    { 0x8086, 0x1029 },
    { 0x8086, 0x103A },
    { 0x8086, 0x2459 },
    { 0x8086, 0x245D },
    { 0x8086, 0x266C },
    { 0x8086, 0x1064 },
    { 0x8086, 0x1068 },
    { 0x8086, 0x27DC },
    { 0x8086, 0x1069 }
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
    api->register_net("fxp", false, mac, true, 1000, base);
    return 0;
}
