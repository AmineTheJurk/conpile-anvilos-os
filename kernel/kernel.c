#include "types.h"
#include "bootinfo.h"
#include "gdt.h"
#include "idt.h"
#include "mem.h"
#include "serial.h"
#include "timer.h"
#include "keyboard.h"
#include "mouse.h"
#include "ata.h"
#include "pci.h"
#include "anvilfs.h"
#include "vfs.h"
#include "task.h"
#include "anv.h"
#include "fb.h"
#include "kmod.h"
#include "wifi.h"
#include "gui.h"
#include "io.h"
#include "string.h"

void kmain(const boot_info_t* bi) {
    serial_init();
    serial_puts("\n========================================\n");
    serial_puts("  AnvilOS Kernel v1.0 Booting...\n");
    serial_puts("========================================\n");

    if (!bi || bi->magic != BOOTINFO_MAGIC) {
        serial_puts("[KERNEL] FATAL: Invalid BootInfo magic!\n");
        for (;;) { cli(); hlt(); }
    }

    gdt_init();
    idt_init();
    serial_puts("[CPU] GDT & IDT initialized.\n");

    pmm_init(bi);
    vmm_init(bi);
    kheap_init(0x00800000, 6 * 1024 * 1024); /* 6 MiB Kernel Heap at 8MB */
    serial_printf("[MEM] PMM=%u KB, Paging enabled, Heap=6144 KB\n", pmm_total_kb());

    timer_init(100);
    keyboard_init();
    mouse_init(bi->vbe_active ? (int32_t)bi->fb_width : 1024,
               bi->vbe_active ? (int32_t)bi->fb_height : 768);
    sti();
    serial_puts("[DEV] PIT (100Hz), PS/2 Keyboard & Mouse online.\n");

    pci_init();
    serial_printf("[PCI] Found %d hardware devices.\n", pci_device_count());

    if (ata_init()) {
        serial_printf("[ATA] Primary Master: %s (%u sectors)\n", ata_model_name(), ata_sector_count());
    } else {
        serial_puts("[ATA] No IDE drive detected; will use Stage2 RAM-Disk.\n");
    }

    if (!anvilfs_mount(bi->initrd_addr, bi->initrd_size)) {
        serial_puts("[FS] FATAL: Failed to mount AnvilFS!\n");
        for (;;) { hlt(); }
    }

    vfs_init();
    task_init();

    if (fb_init(bi)) {
        serial_printf("[VBE] Framebuffer %ux%ux%u @ 0x%08x\n",
                      bi->fb_width, bi->fb_height, bi->fb_bpp, bi->fb_addr);
    } else {
        serial_puts("[VBE] Headless / text mode.\n");
    }

    /* Probe hardware on demand & load ONLY matching OpenBSD .mod drivers from /lib/modules */
    kmod_probe_and_load(bi);
    if (kmod_unmatched_count() > 0) {
        kmod_prompt_incompatible(bi);
    }

    wifi_init();
    ash_init();
    serial_puts("[OK] Kernel ready. Entering Desktop / Shell loop.\n");
    gui_run();
}
