#include "mem.h"
#include "string.h"
#include "serial.h"

/* Physical Page Frame Allocator (supports up to 128 MiB = 32768 pages) */
#define PMM_MAX_PAGES 32768
static uint32_t pmm_bitmap[PMM_MAX_PAGES / 32];
static uint32_t pmm_total_pages = 0;
static uint32_t pmm_used_pages  = 0;

static void pmm_set_bit(uint32_t page) {
    if (page < PMM_MAX_PAGES) {
        if (!(pmm_bitmap[page / 32] & (1U << (page % 32)))) {
            pmm_bitmap[page / 32] |= (1U << (page % 32));
            pmm_used_pages++;
        }
    }
}

static void pmm_clear_bit(uint32_t page) {
    if (page < PMM_MAX_PAGES) {
        if (pmm_bitmap[page / 32] & (1U << (page % 32))) {
            pmm_bitmap[page / 32] &= ~(1U << (page % 32));
            if (pmm_used_pages > 0) pmm_used_pages--;
        }
    }
}

void pmm_init(const boot_info_t* bi) {
    memset(pmm_bitmap, 0xFF, sizeof(pmm_bitmap));
    pmm_total_pages = 0;
    pmm_used_pages  = PMM_MAX_PAGES;

    uint64_t highest = 0;
    for (uint16_t i = 0; i < bi->e820_count; i++) {
        const e820_entry_t* e = &bi->e820[i];
        if (e->type == 1) {
            uint64_t end = e->base + e->length;
            if (end > highest) highest = end;
            uint64_t start_p = ALIGN_UP(e->base, PAGE_SIZE) / PAGE_SIZE;
            uint64_t end_p   = ALIGN_DOWN(end, PAGE_SIZE) / PAGE_SIZE;
            for (uint64_t p = start_p; p < end_p && p < PMM_MAX_PAGES; p++) {
                pmm_clear_bit((uint32_t)p);
            }
        }
    }

    if (highest > (uint64_t)PMM_MAX_PAGES * PAGE_SIZE) {
        pmm_total_pages = PMM_MAX_PAGES;
    } else {
        pmm_total_pages = (uint32_t)(highest / PAGE_SIZE);
    }

    /* Reserve low 8 MiB (BIOS, kernel, ramdisk, page tables, initial heap) */
    for (uint32_t p = 0; p < 2048; p++) {
        pmm_set_bit(p);
    }
}

uint32_t pmm_alloc_page(void) {
    for (uint32_t i = 0; i < pmm_total_pages / 32; i++) {
        if (pmm_bitmap[i] != 0xFFFFFFFF) {
            for (uint32_t b = 0; b < 32; b++) {
                if (!(pmm_bitmap[i] & (1U << b))) {
                    uint32_t page = i * 32 + b;
                    pmm_set_bit(page);
                    return page * PAGE_SIZE;
                }
            }
        }
    }
    return 0;
}

void pmm_free_page(uint32_t phys_addr) {
    uint32_t page = phys_addr / PAGE_SIZE;
    if (page >= 2048 && page < pmm_total_pages) {
        pmm_clear_bit(page);
    }
}

uint32_t pmm_total_kb(void) {
    return pmm_total_pages * 4;
}

uint32_t pmm_used_kb(void) {
    return pmm_used_pages * 4;
}

/* 32-bit x86 Paging (4KB pages, static page directory + page tables for low 32MB + dynamic LFB tables) */
static uint32_t page_directory[1024] __attribute__((aligned(4096)));
static uint32_t low_page_tables[8][1024] __attribute__((aligned(4096))); /* 8 * 4MB = 32MB */
static uint32_t lfb_page_tables[12][1024] __attribute__((aligned(4096))); /* up to 48MB LFB + PCI MMIO */
static uint32_t lfb_pt_used = 0;

void vmm_map_region(uint32_t virt, uint32_t phys, uint32_t size, uint32_t flags) {
    uint32_t v = ALIGN_DOWN(virt, PAGE_SIZE);
    uint32_t p = ALIGN_DOWN(phys, PAGE_SIZE);
    uint32_t end = ALIGN_UP(virt + size, PAGE_SIZE);

    while (v < end) {
        uint32_t pd_idx = v >> 22;
        uint32_t pt_idx = (v >> 12) & 0x3FF;
        uint32_t* pt = NULL;

        if (page_directory[pd_idx] & 1) {
            pt = (uint32_t*)(page_directory[pd_idx] & ~0xFFFU);
        } else if (lfb_pt_used < 12) {
            pt = lfb_page_tables[lfb_pt_used++];
            memset(pt, 0, PAGE_SIZE);
            page_directory[pd_idx] = ((uint32_t)pt) | 0x03;
        }
        if (pt) {
            pt[pt_idx] = p | (flags & 0xFFFU) | 0x01;
        }
        v += PAGE_SIZE;
        p += PAGE_SIZE;
    }
}

void vmm_init(const boot_info_t* bi) {
    memset(page_directory, 0, sizeof(page_directory));
    lfb_pt_used = 0;

    /* Identity map first 32 MiB */
    for (uint32_t pd = 0; pd < 8; pd++) {
        for (uint32_t pt = 0; pt < 1024; pt++) {
            uint32_t addr = (pd * 1024 + pt) * PAGE_SIZE;
            low_page_tables[pd][pt] = addr | 0x03; /* Present + RW */
        }
        page_directory[pd] = ((uint32_t)low_page_tables[pd]) | 0x03;
    }

    /* Identity map VESA Linear Framebuffer if active */
    if (bi->vbe_active && bi->fb_addr != 0) {
        uint32_t fb_bytes = bi->fb_pitch * bi->fb_height;
        if (fb_bytes < 4 * 1024 * 1024) fb_bytes = 4 * 1024 * 1024;
        vmm_map_region(bi->fb_addr, bi->fb_addr, fb_bytes, 0x03);
    }

    /* Load CR3 and enable CR0.PG */
    __asm__ volatile ("mov %0, %%cr3" : : "r"(page_directory));
    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0));
}

/* Kernel Heap: first-fit block allocator with coalescing */
typedef struct heap_blk {
    uint32_t size;
    uint8_t  free;
    struct heap_blk* next;
} heap_blk_t;

static heap_blk_t* heap_head = NULL;
static uint32_t    heap_total = 0;

void kheap_init(uint32_t start_addr, uint32_t size) {
    heap_head = (heap_blk_t*)start_addr;
    heap_head->size = size - sizeof(heap_blk_t);
    heap_head->free = 1;
    heap_head->next = NULL;
    heap_total = size;
}

void* kmalloc(size_t size) {
    if (size == 0) return NULL;
    size = ALIGN_UP(size, 8);

    heap_blk_t* cur = heap_head;
    while (cur) {
        if (cur->free && cur->size >= size) {
            if (cur->size >= size + sizeof(heap_blk_t) + 16) {
                heap_blk_t* split = (heap_blk_t*)((uint8_t*)cur + sizeof(heap_blk_t) + size);
                split->size = cur->size - size - sizeof(heap_blk_t);
                split->free = 1;
                split->next = cur->next;
                cur->size = size;
                cur->next = split;
            }
            cur->free = 0;
            return (void*)((uint8_t*)cur + sizeof(heap_blk_t));
        }
        cur = cur->next;
    }
    serial_printf("[MEM] kmalloc(%u) failed!\n", (uint32_t)size);
    return NULL;
}

void* kcalloc(size_t n, size_t size) {
    size_t total = n * size;
    void* p = kmalloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void kfree(void* ptr) {
    if (!ptr) return;
    heap_blk_t* blk = (heap_blk_t*)((uint8_t*)ptr - sizeof(heap_blk_t));
    blk->free = 1;

    /* Coalesce adjacent free blocks */
    heap_blk_t* cur = heap_head;
    while (cur && cur->next) {
        if (cur->free && cur->next->free) {
            cur->size += sizeof(heap_blk_t) + cur->next->size;
            cur->next = cur->next->next;
        } else {
            cur = cur->next;
        }
    }
}

uint32_t kheap_used_bytes(void) {
    uint32_t used = 0;
    heap_blk_t* cur = heap_head;
    while (cur) {
        if (!cur->free) used += cur->size + sizeof(heap_blk_t);
        cur = cur->next;
    }
    return used;
}

uint32_t kheap_total_bytes(void) {
    return heap_total;
}
