#include "kmod.h"
#include "pci.h"
#include "anvilfs.h"
#include "mem.h"
#include "io.h"
#include "serial.h"
#include "string.h"
#include "fb.h"
#include "keyboard.h"
#include "mouse.h"
#include "acpi.h"
#include "timer.h"

static loaded_mod_info_t    loaded_mods[KMOD_MAX_LOADED];
static int                  loaded_cnt = 0;

static unmatched_dev_info_t unmatched_devs[KMOD_MAX_UNMATCHED];
static int                  unmatched_cnt = 0;

static net_iface_info_t     net_ifaces[KMOD_MAX_NET];
static int                  net_cnt = 0;

static gpu_adapter_info_t   gpu_adapters[KMOD_MAX_GPU];
static int                  gpu_cnt = 0;

static const boot_info_t*   g_boot_info = NULL;

/* Port I/O wrappers exported to loadable .mod binaries */
static uint8_t  api_inb(uint16_t port) { return inb(port); }
static void     api_outb(uint16_t port, uint8_t val) { outb(port, val); }
static uint16_t api_inw(uint16_t port) { return inw(port); }
static void     api_outw(uint16_t port, uint16_t val) { outw(port, val); }
static uint32_t api_inl(uint16_t port) { return inl(port); }
static void     api_outl(uint16_t port, uint32_t val) { outl(port, val); }

static void* api_vmm_map_mmio(uint32_t phys, uint32_t size) {
    if (!phys || !size) return NULL;
    vmm_map_region(phys, phys, size, 0x1B); /* Present + RW + PWT + PCD */
    return (void*)phys;
}

static void api_register_gpu(const char* drv, const char* vendor, uint16_t did,
                             uint32_t mmio_phys, uint32_t vram_mb, const char* note) {
    if (gpu_cnt >= KMOD_MAX_GPU) return;
    gpu_adapter_info_t* g = &gpu_adapters[gpu_cnt++];
    memset(g, 0, sizeof(*g));
    if (drv)    strncpy(g->driver, drv, sizeof(g->driver) - 1);
    if (vendor) strncpy(g->vendor, vendor, sizeof(g->vendor) - 1);
    g->device_id = did;
    g->mmio_phys = mmio_phys;
    g->vram_mb   = vram_mb;
    if (note)   strncpy(g->note, note, sizeof(g->note) - 1);
}

static void api_register_fb(const char* drv, uint32_t phys, uint32_t w, uint32_t h, uint32_t bpp) {
    serial_printf("[FB-MOD] %s bound %ux%ux%u @ 0x%08x\n",
                  drv ? drv : "fb", w, h, bpp, phys);
}

static void api_register_net(const char* drv, bool is_wifi, const uint8_t mac[6],
                             bool link_up, uint32_t speed_mbps, uint32_t base_addr) {
    if (net_cnt >= KMOD_MAX_NET) return;
    net_iface_info_t* n = &net_ifaces[net_cnt++];
    memset(n, 0, sizeof(*n));
    snprintf(n->iface, sizeof(n->iface), "%s%d", drv ? drv : (is_wifi ? "wlan" : "eth"), net_cnt - 1);
    if (drv) strncpy(n->driver, drv, sizeof(n->driver) - 1);
    n->is_wifi    = is_wifi;
    n->link_up    = link_up;
    n->speed_mbps = speed_mbps;
    n->base_addr  = base_addr;
    if (mac) memcpy(n->mac, mac, 6);
}

static kmod_api_t g_kmod_api;

static void init_kmod_api(const boot_info_t* bi) {
    g_boot_info = bi;
    g_kmod_api.abi_version          = 1;
    g_kmod_api.log                  = serial_printf;
    g_kmod_api.kmalloc              = kmalloc;
    g_kmod_api.kfree                = kfree;
    g_kmod_api.vmm_map_mmio         = api_vmm_map_mmio;
    g_kmod_api.pci_read32           = pci_cfg_read32;
    g_kmod_api.pci_write32          = pci_cfg_write32;
    g_kmod_api.pci_enable_busmaster = pci_enable_busmaster;
    g_kmod_api.inb                  = api_inb;
    g_kmod_api.outb                 = api_outb;
    g_kmod_api.inw                  = api_inw;
    g_kmod_api.outw                 = api_outw;
    g_kmod_api.inl                  = api_inl;
    g_kmod_api.outl                 = api_outl;
    g_kmod_api.register_gpu         = api_register_gpu;
    g_kmod_api.register_fb          = api_register_fb;
    g_kmod_api.register_net         = api_register_net;
    g_kmod_api.boot_info            = bi;
}

typedef int (*mod_entry_fn)(const kmod_api_t* api, const kmod_pci_dev_t* dev);

static void pci_to_kmod_dev(const pci_device_t* src, kmod_pci_dev_t* dst) {
    memset(dst, 0, sizeof(*dst));
    if (!src) return;
    dst->bus        = src->bus;
    dst->slot       = src->slot;
    dst->func       = src->func;
    dst->vendor_id  = src->vendor_id;
    dst->device_id  = src->device_id;
    dst->class_code = src->class_code;
    dst->subclass   = src->subclass;
    dst->prog_if    = src->prog_if;
    dst->irq_line   = src->irq_line;
    for (int i = 0; i < 6; i++) dst->bar[i] = src->bar[i];
}

static int find_loaded_index(const char* name) {
    for (int i = 0; i < loaded_cnt; i++) {
        if (strcmp(loaded_mods[i].name, name) == 0) return i;
    }
    return -1;
}

bool kmod_load_by_name(const char* name, const kmod_pci_dev_t* opt_dev) {
    if (!name || !*name || loaded_cnt >= KMOD_MAX_LOADED) return false;

    int existing = find_loaded_index(name);
    if (existing >= 0) {
        /* Module already resident; if binding another PCI device, re-invoke init entry */
        if (opt_dev) {
            /* Module binary header is not retained separately, re-read entry only if needed;
               for multi-NIC systems we load an instance cleanly below if distinct */
        }
        return true;
    }

    char path[64];
    snprintf(path, sizeof(path), "/lib/modules/%s.mod", name);
    anvilfs_inode_t st;
    if (!anvilfs_stat(path, &st) || st.type != ANVILFS_TYPE_FILE || st.size < sizeof(akm_header_t)) {
        return false;
    }

    uint8_t* raw = (uint8_t*)kmalloc(st.size);
    if (!raw) return false;
    int got = anvilfs_read_file(path, raw, st.size);
    if (got < (int)sizeof(akm_header_t)) {
        kfree(raw);
        return false;
    }

    const akm_header_t* hdr = (const akm_header_t*)raw;
    if (hdr->magic != AKM_MAGIC || hdr->version != 1) {
        kfree(raw);
        return false;
    }

    uint32_t data_off = sizeof(akm_header_t)
                      + hdr->id_count * sizeof(mod_pci_id_t)
                      + hdr->reloc_count * sizeof(uint32_t);
    if (data_off + hdr->payload_size > (uint32_t)got) {
        kfree(raw);
        return false;
    }

    uint32_t total_mem = hdr->payload_size + hdr->bss_size;
    if (total_mem < 16) total_mem = 16;
    uint8_t* mem = (uint8_t*)kmalloc(total_mem);
    if (!mem) {
        kfree(raw);
        return false;
    }
    memset(mem, 0, total_mem);
    memcpy(mem, raw + data_off, hdr->payload_size);

    /* Apply R_386_32 base relocations */
    const uint32_t* rels = (const uint32_t*)(raw + sizeof(akm_header_t) + hdr->id_count * sizeof(mod_pci_id_t));
    for (uint32_t r = 0; r < hdr->reloc_count; r++) {
        uint32_t off = rels[r];
        if (off + 4 <= hdr->payload_size) {
            uint32_t* target = (uint32_t*)(mem + off);
            *target += (uint32_t)mem;
        }
    }

    mod_entry_fn entry = (mod_entry_fn)(mem + hdr->entry_off);
    kmod_pci_dev_t kdev;
    if (opt_dev) kdev = *opt_dev;
    else memset(&kdev, 0, sizeof(kdev));

    int rc = entry(&g_kmod_api, &kdev);

    loaded_mod_info_t* lm = &loaded_mods[loaded_cnt++];
    memset(lm, 0, sizeof(*lm));
    strncpy(lm->name, hdr->name, sizeof(lm->name) - 1);
    strncpy(lm->origin, hdr->origin, sizeof(lm->origin) - 1);
    lm->drv_class = hdr->drv_class;
    lm->load_addr = (uint32_t)mem;
    lm->mem_bytes = total_mem;
    lm->bus       = kdev.bus;
    lm->slot      = kdev.slot;
    lm->func      = kdev.func;
    lm->vendor_id = kdev.vendor_id;
    lm->device_id = kdev.device_id;

    kfree(raw);
    serial_printf("[KMOD] Loaded %s.mod @ 0x%08x (%uB, %s) -> rc=%d\n",
                  lm->name, lm->load_addr, lm->mem_bytes, lm->origin, rc);
    return rc == 0;
}

bool kmod_unload_by_name(const char* name) {
    int idx = find_loaded_index(name);
    if (idx < 0) return false;
    kfree((void*)loaded_mods[idx].load_addr);
    for (int i = idx; i < loaded_cnt - 1; i++) {
        loaded_mods[i] = loaded_mods[i + 1];
    }
    loaded_cnt--;
    return true;
}

static void add_unmatched(const pci_device_t* d, const char* desc, const char* reason) {
    if (unmatched_cnt >= KMOD_MAX_UNMATCHED) return;
    unmatched_dev_info_t* u = &unmatched_devs[unmatched_cnt++];
    memset(u, 0, sizeof(*u));
    if (d) {
        u->bus        = d->bus;
        u->slot       = d->slot;
        u->func       = d->func;
        u->vendor_id  = d->vendor_id;
        u->device_id  = d->device_id;
        u->class_code = d->class_code;
        u->subclass   = d->subclass;
    }
    strncpy(u->desc, desc ? desc : "Unknown PCI Device", sizeof(u->desc) - 1);
    strncpy(u->reason, reason ? reason : "No compatible .mod driver", sizeof(u->reason) - 1);
}

void kmod_probe_and_load(const boot_info_t* bi) {
    loaded_cnt    = 0;
    unmatched_cnt = 0;
    net_cnt       = 0;
    gpu_cnt       = 0;
    init_kmod_api(bi);

    /* Read fast binary module index /lib/modules/modules.idx */
    anvilfs_inode_t idx_st;
    uint8_t* idx_buf = NULL;
    const amix_header_t* ah = NULL;
    const amix_mod_entry_t* mods = NULL;
    const amix_id_entry_t* ids = NULL;

    if (anvilfs_stat("/lib/modules/modules.idx", &idx_st) &&
        idx_st.type == ANVILFS_TYPE_FILE &&
        idx_st.size >= sizeof(amix_header_t)) {
        idx_buf = (uint8_t*)kmalloc(idx_st.size);
        if (idx_buf && anvilfs_read_file("/lib/modules/modules.idx", idx_buf, idx_st.size) == (int)idx_st.size) {
            const amix_header_t* tmp = (const amix_header_t*)idx_buf;
            if (tmp->magic == AMIX_MAGIC && tmp->version == 1) {
                ah   = tmp;
                mods = (const amix_mod_entry_t*)(idx_buf + sizeof(amix_header_t));
                ids  = (const amix_id_entry_t*)(idx_buf + sizeof(amix_header_t) +
                                                ah->mod_count * sizeof(amix_mod_entry_t));
                serial_printf("[KMOD] Indexed %u OpenBSD modules (%u PCI IDs) in /lib/modules/modules.idx\n",
                              ah->mod_count, ah->id_count);
            }
        }
    } else {
        serial_puts("[KMOD] WARNING: /lib/modules/modules.idx missing!\n");
    }

    /* 1. Framebuffer module: load ONLY vesafb or efifb matching boot framebuffer */
    bool fb_mod_ok = false;
    if (bi && bi->vbe_active) {
        const char* fb_drv = (bi->vbe_active == 2) ? "efifb" : "vesafb";
        fb_mod_ok = kmod_load_by_name(fb_drv, NULL);
    }

    /* 2. Probe every discovered PCI device on demand */
    int n = pci_device_count();
    for (int i = 0; i < n; i++) {
        const pci_device_t* d = pci_get_device(i);
        if (!d) continue;

        /* Core motherboard chipsets (Host/ISA/PCI bridges) & boot IDE controller */
        if (d->class_code == 0x06) continue;
        if (d->class_code == 0x01 && d->subclass == 0x01) continue;

        const char* matched_mod = NULL;
        if (ah && ids && mods) {
            /* Exact PCI Vendor:Device lookup across all 2,500+ OpenBSD IDs */
            for (uint32_t j = 0; j < ah->id_count; j++) {
                if (ids[j].vendor_id == d->vendor_id && ids[j].device_id == d->device_id) {
                    if (ids[j].mod_index < ah->mod_count) {
                        matched_mod = mods[ids[j].mod_index].name;
                        break;
                    }
                }
            }
            /* Fallback class/subclass lookup (e.g. generic PCI VGA -> vgafb.mod) */
            if (!matched_mod) {
                for (uint16_t m = 0; m < ah->mod_count; m++) {
                    if (mods[m].match_class != 0xFF &&
                        mods[m].match_class == d->class_code &&
                        (mods[m].match_sub == 0xFF || mods[m].match_sub == d->subclass)) {
                        matched_mod = mods[m].name;
                        break;
                    }
                }
            }
        }

        if (matched_mod) {
            kmod_pci_dev_t kdev;
            pci_to_kmod_dev(d, &kdev);
            if (!kmod_load_by_name(matched_mod, &kdev)) {
                char desc[64], rsn[48];
                snprintf(desc, sizeof(desc), "%s %s",
                         pci_vendor_name(d->vendor_id), pci_class_name(d->class_code, d->subclass));
                snprintf(rsn, sizeof(rsn), "Failed loading %s.mod", matched_mod);
                add_unmatched(d, desc, rsn);
            }
        } else {
            char desc[64];
            snprintf(desc, sizeof(desc), "%s %s",
                     pci_vendor_name(d->vendor_id), pci_class_name(d->class_code, d->subclass));
            add_unmatched(d, desc, "No compatible OpenBSD .mod found");
        }
    }

    /* Ensure at least one display module is active if VBE was active but vesafb.mod missing */
    if (bi && bi->vbe_active && !fb_mod_ok) {
        add_unmatched(NULL, "VESA/EFI Framebuffer Display", "Missing vesafb.mod / efifb.mod");
    }

    if (idx_buf) kfree(idx_buf);
    serial_printf("[KMOD] Probe complete: %d module(s) loaded, %d incompatible device(s).\n",
                  loaded_cnt, unmatched_cnt);
}

int kmod_loaded_count(void) { return loaded_cnt; }
const loaded_mod_info_t* kmod_get_loaded(int idx) {
    return (idx >= 0 && idx < loaded_cnt) ? &loaded_mods[idx] : NULL;
}

int kmod_unmatched_count(void) { return unmatched_cnt; }
const unmatched_dev_info_t* kmod_get_unmatched(int idx) {
    return (idx >= 0 && idx < unmatched_cnt) ? &unmatched_devs[idx] : NULL;
}

int kmod_net_count(void) { return net_cnt; }
const net_iface_info_t* kmod_get_net(int idx) {
    return (idx >= 0 && idx < net_cnt) ? &net_ifaces[idx] : NULL;
}

int kmod_gpu_count(void) { return gpu_cnt; }
const gpu_adapter_info_t* kmod_get_gpu(int idx) {
    return (idx >= 0 && idx < gpu_cnt) ? &gpu_adapters[idx] : NULL;
}

/* ========================================================================
 * "No Compatible Devices On Your Computer! :" Interactive Boot Prompt
 * Supports Arrow Keys + Enter (VBE GUI + COM1 Serial) and Mouse Clicks
 * ======================================================================== */

static void draw_prompt_cursor(int32_t mx, int32_t my) {
    static const uint16_t cur[12] = {
        0x8000,0xC000,0xE000,0xF000,
        0xF800,0xFC00,0xFE00,0xFF00,
        0xF800,0xD800,0x8C00,0x0C00
    };
    for (int r = 0; r < 12; r++) {
        for (int c = 0; c < 12; c++) {
            if (cur[r] & (0x8000 >> c)) {
                fb_put_pixel(mx + c + 1, my + r + 1, RGB(5, 8, 12));
                fb_put_pixel(mx + c, my + r, RGB(255, 255, 255));
            }
        }
    }
}

static void render_incompatible_screen(int selected_btn, int32_t mx, int32_t my,
                                       int32_t* out_bx0, int32_t* out_bx1, int32_t* out_by) {
    uint32_t sw = fb_width();
    uint32_t sh = fb_height();

    fb_fill_gradient_v(0, 0, (int32_t)sw, (int32_t)sh, RGB(18, 14, 24), RGB(34, 18, 28));

    int32_t box_w = 680;
    if ((int32_t)sw - 40 < box_w) box_w = (int32_t)sw - 40;
    int32_t box_h = 420;
    if ((int32_t)sh - 40 < box_h) box_h = (int32_t)sh - 40;
    int32_t bx = ((int32_t)sw - box_w) / 2;
    int32_t by = ((int32_t)sh - box_h) / 2;

    /* Modal panel + border */
    fb_fill_rect(bx + 6, by + 6, box_w, box_h, RGB(6, 6, 10));
    fb_fill_rect(bx, by, box_w, box_h, RGB(22, 25, 36));
    fb_draw_rect(bx, by, box_w, box_h, RGB(230, 95, 95));

    /* Alert title bar */
    fb_fill_rect(bx, by, box_w, 42, RGB(135, 35, 45));
    fb_draw_string(bx + 20, by + 16,
                   "Compatible Drivers Were Not Found For The Devices Listed Below :",
                   RGB(255, 245, 245), 0, true);

    /* Device list box */
    int32_t lx = bx + 20;
    int32_t ly = by + 80;
    int32_t lw = box_w - 40;
    int32_t lh = box_h - 175;
    fb_fill_rect(lx, ly, lw, lh, RGB(12, 15, 22));
    fb_draw_rect(lx, ly, lw, lh, RGB(65, 78, 105));

    int32_t cy = ly + 12;
    char row[128];
    for (int i = 0; i < unmatched_cnt && cy + 20 < ly + lh; i++) {
        const unmatched_dev_info_t* u = &unmatched_devs[i];
        fb_fill_rect(lx + 8, cy - 2, lw - 16, 22, (i % 2 == 0) ? RGB(20, 25, 36) : RGB(16, 20, 30));
        snprintf(row, sizeof(row), "%s", u->desc);
        fb_draw_string(lx + 14, cy + 4, row, RGB(255, 195, 130), 0, true);
        cy += 26;
    }

    fb_draw_string(bx + 20, by + box_h - 82,
                   "Navigate with Arrow Keys (Left/Right/Up/Down) and press ENTER (or Click):",
                   RGB(165, 190, 225), 0, true);

    int32_t btn_w = 180;
    int32_t btn_h = 38;
    int32_t btn_y = by + box_h - 56;
    int32_t btn0_x = bx + (box_w / 2) - btn_w - 20;
    int32_t btn1_x = bx + (box_w / 2) + 20;

    if (out_bx0) *out_bx0 = btn0_x;
    if (out_bx1) *out_bx1 = btn1_x;
    if (out_by)  *out_by  = btn_y;

    /* Button 0: Continue */
    uint32_t c0_bg = (selected_btn == 0) ? RGB(42, 128, 92) : RGB(34, 44, 62);
    uint32_t c0_bd = (selected_btn == 0) ? RGB(130, 255, 190) : RGB(75, 92, 120);
    fb_fill_rect(btn0_x, btn_y, btn_w, btn_h, c0_bg);
    fb_draw_rect(btn0_x, btn_y, btn_w, btn_h, c0_bd);
    if (selected_btn == 0) fb_draw_rect(btn0_x - 2, btn_y - 2, btn_w + 4, btn_h + 4, RGB(130, 255, 190));
    fb_draw_string(btn0_x + 48, btn_y + 15, "[ Continue ]", RGB(255, 255, 255), 0, true);

    /* Button 1: Shut Down */
    uint32_t c1_bg = (selected_btn == 1) ? RGB(175, 48, 58) : RGB(44, 34, 42);
    uint32_t c1_bd = (selected_btn == 1) ? RGB(255, 165, 165) : RGB(95, 72, 82);
    fb_fill_rect(btn1_x, btn_y, btn_w, btn_h, c1_bg);
    fb_draw_rect(btn1_x, btn_y, btn_w, btn_h, c1_bd);
    if (selected_btn == 1) fb_draw_rect(btn1_x - 2, btn_y - 2, btn_w + 4, btn_h + 4, RGB(255, 165, 165));
    fb_draw_string(btn1_x + 44, btn_y + 15, "[ Shut Down ]", RGB(255, 255, 255), 0, true);

    draw_prompt_cursor(mx, my);
    fb_present();
}

static void print_serial_prompt(int sel) {
    serial_puts("\n====================================================================\n");
    serial_puts("  Compatible Drivers Were Not Found For The Devices Listed Below :\n");
    serial_puts("====================================================================\n");
    for (int i = 0; i < unmatched_cnt; i++) {
        const unmatched_dev_info_t* u = &unmatched_devs[i];
        serial_printf("  * %s\n", u->desc);
    }
    serial_puts("--------------------------------------------------------------------\n");
    serial_printf("  Arrow Keys + Enter:   %s[ Continue ]%s     %s[ Shut Down ]%s\n",
                  sel == 0 ? ">>> " : "    ", sel == 0 ? " <<<" : "    ",
                  sel == 1 ? ">>> " : "    ", sel == 1 ? " <<<" : "    ");
}

void kmod_prompt_incompatible(const boot_info_t* bi) {
    UNUSED(bi);
    if (unmatched_cnt <= 0) return;

    int selected_btn = 0; /* 0 = Continue, 1 = Shut Down */
    int esc_state = 0;    /* serial ANSI arrow sequence parser */
    bool dirty = true;

    print_serial_prompt(selected_btn);

    mouse_state_t ms;
    mouse_get_state(&ms);
    uint8_t prev_btns = ms.buttons;
    int32_t btn0_x = 0, btn1_x = 0, btn_y = 0;

    for (;;) {
        if (dirty && fb_is_active()) {
            render_incompatible_screen(selected_btn, ms.x, ms.y, &btn0_x, &btn1_x, &btn_y);
            dirty = false;
        }

        while (keyboard_has_char()) {
            uint8_t k = (uint8_t)keyboard_getc();
            if (k == KEY_LEFT || k == KEY_UP || k == KEY_RIGHT || k == KEY_DOWN || k == '\t') {
                selected_btn = 1 - selected_btn;
                print_serial_prompt(selected_btn);
                dirty = true;
            } else if (k == '\n' || k == '\r' || k == ' ') {
                if (selected_btn == 1) acpi_shutdown();
                serial_puts("\n[BOOT] User selected Continue -> booting AnvilOS anyway.\n");
                return;
            } else if (k == 'c' || k == 'C') {
                serial_puts("\n[BOOT] User selected Continue -> booting AnvilOS anyway.\n");
                return;
            } else if (k == 's' || k == 'S') {
                acpi_shutdown();
            }
        }

        while (serial_has_char()) {
            uint8_t k = (uint8_t)serial_getc();
            if (esc_state == 0 && k == 0x1B) { esc_state = 1; continue; }
            if (esc_state == 1) { esc_state = (k == '[') ? 2 : 0; continue; }
            if (esc_state == 2) {
                esc_state = 0;
                if (k == 'A' || k == 'B' || k == 'C' || k == 'D') {
                    selected_btn = 1 - selected_btn;
                    print_serial_prompt(selected_btn);
                    dirty = true;
                }
                continue;
            }
            if (k == '\t') {
                selected_btn = 1 - selected_btn;
                print_serial_prompt(selected_btn);
                dirty = true;
            } else if (k == '\r' || k == '\n' || k == ' ') {
                if (selected_btn == 1) acpi_shutdown();
                serial_puts("\n[BOOT] User selected Continue -> booting AnvilOS anyway.\n");
                return;
            } else if (k == 'c' || k == 'C') {
                serial_puts("\n[BOOT] User selected Continue -> booting AnvilOS anyway.\n");
                return;
            } else if (k == 's' || k == 'S') {
                acpi_shutdown();
            }
        }

        if (fb_is_active()) {
            mouse_get_state(&ms);
            if (ms.moved) dirty = true;
            bool left_down = (ms.buttons & 0x01) != 0;
            bool left_prev = (prev_btns & 0x01) != 0;
            if (left_down && !left_prev) {
                if (ms.y >= btn_y && ms.y < btn_y + 38) {
                    if (ms.x >= btn0_x && ms.x < btn0_x + 180) {
                        serial_puts("\n[BOOT] User clicked Continue -> booting AnvilOS anyway.\n");
                        return;
                    }
                    if (ms.x >= btn1_x && ms.x < btn1_x + 180) {
                        acpi_shutdown();
                    }
                }
            }
            prev_btns = ms.buttons;
        }

        if (!dirty) hlt();
    }
}
