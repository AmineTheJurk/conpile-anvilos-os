#ifndef ANVIL_KMOD_H
#define ANVIL_KMOD_H

#ifdef __ANVIL_KERNEL__
#include "types.h"
#include "bootinfo.h"
#else
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#endif

#define AKM_MAGIC       0x314D4B41
#define AMIX_MAGIC      0x58494D41
#define MOD_CLASS_GPU   1
#define MOD_CLASS_FB    2
#define MOD_CLASS_WIFI  3
#define MOD_CLASS_NET   4

typedef struct __attribute__((packed)) {
    uint16_t vendor_id;
    uint16_t device_id;
} mod_pci_id_t;

typedef struct __attribute__((packed)) {
    uint16_t drv_class;
    uint8_t match_class;
    uint8_t match_sub;
    char name[24];
    char origin[40];
} mod_meta_raw_t;

typedef struct {
    char ssid[32];
    uint8_t bssid[6];
    uint8_t signal_pct;
    uint8_t channel;
    bool secured;
} kmod_wifi_bss_t;

#ifdef __ANVIL_KERNEL__
typedef int (*kmod_wifi_scan_fn)(kmod_wifi_bss_t* out, int max_out);
typedef bool (*kmod_wifi_connect_fn)(const char* ssid, const char* passphrase);
typedef void (*kmod_wifi_disconnect_fn)(void);

typedef struct {
    uint8_t bus, slot, func;
    uint16_t vendor_id, device_id;
    uint8_t class_code, subclass, prog_if, irq_line;
    uint32_t bar[6];
} kmod_pci_dev_t;

typedef struct {
    uint32_t abi_version;
    void (*log)(const char* fmt, ...);
    void* (*kmalloc)(size_t sz);
    void (*kfree)(void* p);
    void* (*vmm_map_mmio)(uint32_t phys, uint32_t size);
    uint32_t (*pci_read32)(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off);
    void (*pci_write32)(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint32_t val);
    void (*pci_enable_busmaster)(uint8_t bus, uint8_t slot, uint8_t func);
    uint8_t (*inb)(uint16_t port);
    void (*outb)(uint16_t port, uint8_t val);
    uint16_t (*inw)(uint16_t port);
    void (*outw)(uint16_t port, uint16_t val);
    uint32_t (*inl)(uint16_t port);
    void (*outl)(uint16_t port, uint32_t val);
    void (*register_gpu)(const char* drv, const char* vendor, uint16_t did,
                         uint32_t mmio_phys, uint32_t vram_mb, const char* note);
    void (*register_fb)(const char* drv, uint32_t phys, uint32_t w, uint32_t h, uint32_t bpp);
    void (*register_net)(const char* drv, bool is_wifi, const uint8_t mac[6],
                         bool link_up, uint32_t speed_mbps, uint32_t base_addr);
    void (*register_wifi)(const char* drv, const uint8_t mac[6], bool link_up,
                          uint32_t speed_mbps, uint32_t base_addr,
                          kmod_wifi_scan_fn scan,
                          kmod_wifi_connect_fn connect,
                          kmod_wifi_disconnect_fn disconnect);
    const boot_info_t* boot_info;
} kmod_api_t;

typedef struct {
    char iface[16];
    char driver[24];
    bool is_wifi;
    bool link_up;
    uint32_t speed_mbps;
    uint32_t base_addr;
    uint8_t mac[6];
    kmod_wifi_scan_fn wifi_scan;
    kmod_wifi_connect_fn wifi_connect;
    kmod_wifi_disconnect_fn wifi_disconnect;
} net_iface_info_t;

#define KMOD_MAX_NET 16
int kmod_net_count(void);
const net_iface_info_t* kmod_get_net(int idx);
#endif

#endif
