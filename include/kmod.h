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

#define AKM_MAGIC       0x314D4B41 /* "AKM1" */
#define AMIX_MAGIC      0x58494D41 /* "AMIX" */

#define MOD_CLASS_GPU   1
#define MOD_CLASS_FB    2
#define MOD_CLASS_WIFI  3
#define MOD_CLASS_NET   4

typedef struct __attribute__((packed)) {
    uint16_t vendor_id;
    uint16_t device_id; /* 0xFFFF = wildcard */
} mod_pci_id_t;

typedef struct __attribute__((packed)) {
    uint16_t drv_class;
    uint8_t  match_class; /* 0xFF = exact PCI ID match only */
    uint8_t  match_sub;   /* 0xFF = exact PCI ID match only */
    char     name[24];
    char     origin[40];
} mod_meta_raw_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;        /* "AKM1" */
    uint16_t version;      /* 1 */
    uint16_t drv_class;    /* MOD_CLASS_* */
    uint32_t entry_off;    /* offset into payload for mod_init() */
    uint32_t id_count;     /* number of mod_pci_id_t following header */
    uint32_t reloc_count;  /* number of uint32_t R_386_32 offsets following IDs */
    uint32_t payload_size; /* .text + .rodata + .data size */
    uint32_t bss_size;
    uint8_t  match_class;  /* 0xFF if none */
    uint8_t  match_sub;    /* 0xFF if none */
    uint16_t flags;
    char     name[24];
    char     origin[40];
} akm_header_t;            /* 96 bytes */

/* Fast index in /lib/modules/modules.idx */
typedef struct __attribute__((packed)) {
    uint32_t magic;       /* "AMIX" */
    uint16_t version;     /* 1 */
    uint16_t mod_count;
    uint32_t id_count;
} amix_header_t;

typedef struct __attribute__((packed)) {
    char     name[24];
    uint16_t drv_class;
    uint8_t  match_class;
    uint8_t  match_sub;
} amix_mod_entry_t;       /* 28 bytes */

typedef struct __attribute__((packed)) {
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t mod_index;
    uint16_t reserved;
} amix_id_entry_t;        /* 8 bytes */

#ifdef __ANVIL_KERNEL__
typedef struct {
    uint8_t  bus;
    uint8_t  slot;
    uint8_t  func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  irq_line;
    uint32_t bar[6];
} kmod_pci_dev_t;

typedef struct {
    uint32_t abi_version;
    void     (*log)(const char* fmt, ...);
    void*    (*kmalloc)(size_t sz);
    void     (*kfree)(void* p);
    void*    (*vmm_map_mmio)(uint32_t phys, uint32_t size);
    uint32_t (*pci_read32)(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off);
    void     (*pci_write32)(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint32_t val);
    void     (*pci_enable_busmaster)(uint8_t bus, uint8_t slot, uint8_t func);
    uint8_t  (*inb)(uint16_t port);
    void     (*outb)(uint16_t port, uint8_t val);
    uint16_t (*inw)(uint16_t port);
    void     (*outw)(uint16_t port, uint16_t val);
    uint32_t (*inl)(uint16_t port);
    void     (*outl)(uint16_t port, uint32_t val);
    void     (*register_gpu)(const char* drv, const char* vendor, uint16_t did,
                             uint32_t mmio_phys, uint32_t vram_mb, const char* note);
    void     (*register_fb)(const char* drv, uint32_t phys, uint32_t w, uint32_t h, uint32_t bpp);
    void     (*register_net)(const char* drv, bool is_wifi, const uint8_t mac[6],
                             bool link_up, uint32_t speed_mbps, uint32_t base_addr);
    const boot_info_t* boot_info;
} kmod_api_t;

typedef struct {
    char     name[24];
    char     origin[40];
    uint16_t drv_class;
    uint32_t load_addr;
    uint32_t mem_bytes;
    uint8_t  bus;
    uint8_t  slot;
    uint8_t  func;
    uint16_t vendor_id;
    uint16_t device_id;
} loaded_mod_info_t;

typedef struct {
    uint8_t  bus;
    uint8_t  slot;
    uint8_t  func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    char     desc[64];
    char     reason[48];
} unmatched_dev_info_t;

typedef struct {
    char     iface[16];
    char     driver[24];
    bool     is_wifi;
    bool     link_up;
    uint32_t speed_mbps;
    uint32_t base_addr;
    uint8_t  mac[6];
} net_iface_info_t;

typedef struct {
    char     driver[24];
    char     vendor[24];
    uint16_t device_id;
    uint32_t mmio_phys;
    uint32_t vram_mb;
    char     note[48];
} gpu_adapter_info_t;

#define KMOD_MAX_LOADED    32
#define KMOD_MAX_UNMATCHED 24
#define KMOD_MAX_NET       16
#define KMOD_MAX_GPU       8

void kmod_probe_and_load(const boot_info_t* bi);
int  kmod_loaded_count(void);
const loaded_mod_info_t* kmod_get_loaded(int idx);
int  kmod_unmatched_count(void);
const unmatched_dev_info_t* kmod_get_unmatched(int idx);
int  kmod_net_count(void);
const net_iface_info_t* kmod_get_net(int idx);
int  kmod_gpu_count(void);
const gpu_adapter_info_t* kmod_get_gpu(int idx);
bool kmod_load_by_name(const char* name, const kmod_pci_dev_t* opt_dev);
bool kmod_unload_by_name(const char* name);
void kmod_prompt_incompatible(const boot_info_t* bi);
#endif

#endif
