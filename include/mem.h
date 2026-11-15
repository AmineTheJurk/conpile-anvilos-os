#ifndef ANVIL_MEM_H
#define ANVIL_MEM_H

#include "types.h"
#include "bootinfo.h"

#define PAGE_SIZE 4096

void     pmm_init(const boot_info_t* bi);
uint32_t pmm_alloc_page(void);
void     pmm_free_page(uint32_t phys_addr);
uint32_t pmm_total_kb(void);
uint32_t pmm_used_kb(void);

void     vmm_init(const boot_info_t* bi);
void     vmm_map_region(uint32_t virt, uint32_t phys, uint32_t size, uint32_t flags);

void     kheap_init(uint32_t start_addr, uint32_t size);
void*    kmalloc(size_t size);
void*    kcalloc(size_t n, size_t size);
void     kfree(void* ptr);
uint32_t kheap_used_bytes(void);
uint32_t kheap_total_bytes(void);

#endif
