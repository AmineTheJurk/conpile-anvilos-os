#ifndef ANVIL_BOOTINFO_H
#define ANVIL_BOOTINFO_H

#include "types.h"

#define BOOTINFO_MAGIC 0x42564E41 /* "ANVB" */
#define BOOTINFO_ADDR  0x00005000
#define MAX_E820_ENTRIES 32

typedef struct __attribute__((packed)) {
    uint64_t base;
    uint64_t length;
    uint32_t type;   /* 1 = usable RAM, 2 = reserved, 3 = ACPI reclaimable */
    uint32_t acpi;
} e820_entry_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t boot_drive;
    uint32_t fb_addr;
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t fb_pitch;
    uint8_t  fb_bpp;
    uint8_t  vbe_active;
    uint16_t e820_count;
    uint32_t initrd_addr;
    uint32_t initrd_size;
    uint32_t kernel_size;
    e820_entry_t e820[MAX_E820_ENTRIES];
} boot_info_t;

#endif
