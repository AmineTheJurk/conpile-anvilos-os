#include "gui.h"
#include "fb.h"
#include "vfs.h"
#include "anvilfs.h"
#include "anv.h"
#include "pci.h"
#include "mem.h"
#include "timer.h"
#include "ata.h"
#include "kmod.h"
#include "anvilos_icon.h"
#include "string.h"

const uint16_t icon_terminal[16] = {
    0xFFFF,0x8001,0x8001,0xFFFF,
    0x8001,0x8801,0x8401,0x8201,
    0x8401,0x88F1,0x8001,0x8001,
    0x8001,0x8001,0x8001,0xFFFF
};

const uint16_t icon_files[16] = {
    0x0000,0x7E00,0x4100,0x7FFF,
    0x4001,0x4001,0x4001,0x4001,
    0x4001,0x4001,0x4001,0x4001,
    0x4001,0x4001,0x7FFF,0x0000
};

const uint16_t icon_sysinfo[16] = {
    0x07E0,0x1FF8,0x381C,0x6186,
    0x4182,0xC003,0xC183,0xC183,
    0xC183,0xC183,0xC183,0x4002,
    0x6006,0x381C,0x1FF8,0x07E0
};

const uint16_t icon_editor[16] = {
    0x3FFE,0x2002,0x27E2,0x2002,
    0x2FF2,0x2002,0x2FE2,0x2002,
    0x27C2,0x2002,0x2382,0x2002,
    0x2002,0x2002,0x3FFE,0x0000
};

const uint16_t icon_anv[16] = {
    0x0180,0x03C0,0x07E0,0x0FF0,
    0x1DB8,0x399C,0x718E,0x7FFE,
    0x7FFE,0x718E,0x399C,0x1DB8,
    0x0FF0,0x07E0,0x03C0,0x0180
};

#define FM_MAX_ITEMS 128
char fm_cwd[128] = "/";
anvilfs_dirent_t fm_items[FM_MAX_ITEMS];
int fm_count = 0;

void fm_refresh(void) {
    fm_count = vfs_readdir(fm_cwd, fm_items, FM_MAX_ITEMS);
    if (fm_count < 0) fm_count = 0;
}

void fm_draw(int32_t x, int32_t y, int32_t w, int32_t h) {
    fb_fill_rect(x, y, w, h, RGB(24, 30, 42));
    fb_fill_rect(x + 8, y + 8, w - 16, 22, RGB(15, 20, 30));
    fb_draw_rect(x + 8, y + 8, w - 16, 22, RGB(60, 80, 110));
    char hdr[160];
    snprintf(hdr, sizeof(hdr), "Path: %s", fm_cwd);
    fb_draw_string(x + 14, y + 15, hdr, RGB(160, 220, 255), 0, true);

    fb_fill_rect(x + w - 58, y + 10, 46, 18, RGB(50, 75, 110));
    fb_draw_string(x + w - 48, y + 15, "[Up]", RGB(240, 245, 255), 0, true);

    int32_t ry = y + 38;
    for (int i = 0; i < fm_count && ry + 20 < y + h - 8; i++) {
        char child[160];
        if (strcmp(fm_cwd, "/") == 0) snprintf(child, sizeof(child), "/%s", fm_items[i].name);
        else snprintf(child, sizeof(child), "%s/%s", fm_cwd, fm_items[i].name);
        vfs_stat_t st;
        bool ok = vfs_stat(child, &st);
        bool is_dir = ok && st.type == ANVILFS_TYPE_DIR;
        bool is_anv = !is_dir && strlen(fm_items[i].name) > 4 &&
                      strcmp(fm_items[i].name + strlen(fm_items[i].name) - 4, ".anv") == 0;

        fb_fill_rect(x + 8, ry, w - 16, 18, (i % 2 == 0) ? RGB(30, 37, 52) : RGB(26, 32, 46));
        uint32_t col = is_dir ? RGB(130, 200, 255) : (is_anv ? RGB(120, 245, 160) : RGB(225, 230, 240));
        const char* tag = is_dir ? "[DIR]" : (is_anv ? "[ANV]" : "[FIL]");
        fb_draw_string(x + 14, ry + 5, tag, col, 0, true);
        fb_draw_string(x + 62, ry + 5, fm_items[i].name, col, 0, true);

        char sz_buf[24];
        if (!is_dir && ok) {
            snprintf(sz_buf, sizeof(sz_buf), "%u B", st.size);
            fb_draw_string(x + w - 76, ry + 5, sz_buf, RGB(150, 160, 180), 0, true);
        }
        ry += 20;
    }
}

void fm_click(int32_t rx, int32_t ry, int32_t w) {
    if (rx >= w - 58 && rx <= w - 12 && ry >= 10 && ry <= 28) {
        char next[128];
        vfs_normalize_path(fm_cwd, "..", next, sizeof(next));
        strcpy(fm_cwd, next);
        fm_refresh();
        return;
    }
    if (ry >= 38) {
        int idx = (ry - 38) / 20;
        if (idx >= 0 && idx < fm_count) {
            char child[160];
            if (strcmp(fm_cwd, "/") == 0) snprintf(child, sizeof(child), "/%s", fm_items[idx].name);
            else snprintf(child, sizeof(child), "%s/%s", fm_cwd, fm_items[idx].name);
            vfs_stat_t st;
            if (vfs_stat(child, &st)) {
                if (st.type == ANVILFS_TYPE_DIR) {
                    vfs_normalize_path(fm_cwd, fm_items[idx].name, fm_cwd, sizeof(fm_cwd));
                    fm_refresh();
                } else if (strlen(fm_items[idx].name) > 4 &&
                           strcmp(fm_items[idx].name + strlen(fm_items[idx].name) - 4, ".anv") == 0) {
                    gui_run_anv_popup(child);
                } else {
                    gui_open_editor(child);
                }
            }
        }
    }
}

void sysinfo_draw(int32_t x, int32_t y, int32_t w, int32_t h) {
    fb_fill_rect(x, y, w, h, RGB(20, 26, 36));
    char line[128];
    int32_t cy = y + 12;

    fb_fill_rect(x + w - 48, y + 8, 36, 36, RGB(242, 247, 255));
    fb_draw_rect(x + w - 48, y + 8, 36, 36, RGB(88, 166, 255));
    fb_draw_argb_icon(x + w - 46, y + 10, ANVIL_ICON_LG_W, ANVIL_ICON_LG_H, anvilos_icon_lg);

    fb_draw_string(x + 12, cy, "AnvilOS Kernel & Hardware Telemetry", RGB(110, 210, 255), 0, true);
    cy += 16;

    snprintf(line, sizeof(line), "Kernel: AnvilOS 1.0 i686 | Uptime: %u.%us",
             timer_uptime_ms() / 1000, (timer_uptime_ms() % 1000) / 100);
    fb_draw_string(x + 12, cy, line, RGB(220, 230, 245), 0, true);
    cy += 13;

    snprintf(line, sizeof(line), "RAM: %u KB used / %u KB total | Heap: %u/%u KB",
             pmm_used_kb(), pmm_total_kb(), kheap_used_bytes() / 1024, kheap_total_bytes() / 1024);
    fb_draw_string(x + 12, cy, line, RGB(180, 230, 190), 0, true);
    cy += 14;

    uint32_t tb, fb_blk, ti, fi;
    anvilfs_statfs(&tb, &fb_blk, &ti, &fi);
    snprintf(line, sizeof(line), "AnvilFS: %s | Blocks: %u/%u | Inodes: %u free",
             anvilfs_backend_name(), tb - fb_blk, tb, fi);
    fb_draw_string(x + 12, cy, line, RGB(245, 215, 140), 0, true);
    cy += 16;

    int mc = kmod_loaded_count();
    snprintf(line, sizeof(line), "Loaded .mod Drivers (%d active, unmatched=%d):",
             mc, kmod_unmatched_count());
    fb_draw_string(x + 12, cy, line, RGB(120, 245, 175), 0, true);
    cy += 13;
    for (int i = 0; i < mc && cy + 14 < y + h - 8; i++) {
        const loaded_mod_info_t* m = kmod_get_loaded(i);
        snprintf(line, sizeof(line), " * %-10s [%04x:%04x] %s",
                 m->name, m->vendor_id, m->device_id, m->origin);
        fb_draw_string(x + 12, cy, line, RGB(195, 235, 210), 0, true);
        cy += 12;
    }

    if (cy + 26 < y + h - 8) {
        fb_draw_string(x + 12, cy + 2, "PCI Hardware Bus:", RGB(110, 210, 255), 0, true);
        cy += 16;
        int n = pci_device_count();
        for (int i = 0; i < n && cy + 12 < y + h - 8; i++) {
            const pci_device_t* d = pci_get_device(i);
            snprintf(line, sizeof(line), " %02x:%02x.%x [%04x:%04x] %s",
                     d->bus, d->slot, d->func, d->vendor_id, d->device_id,
                     pci_class_name(d->class_code, d->subclass));
            fb_draw_string(x + 12, cy, line, RGB(190, 200, 220), 0, true);
            cy += 12;
        }
    }
}

char editor_path[128] = "/home/user/readme.txt";
char editor_buf[2048];
int  editor_len = 0;
char editor_status[64] = "Ready";

void editor_load(const char* path) {
    strncpy(editor_path, path, sizeof(editor_path) - 1);
    editor_path[sizeof(editor_path) - 1] = '\0';
    memset(editor_buf, 0, sizeof(editor_buf));
    int fd = vfs_open(editor_path, O_RDONLY);
    if (fd >= 0) {
        int n = vfs_read(fd, editor_buf, sizeof(editor_buf) - 1);
        editor_len = (n > 0) ? n : 0;
        editor_buf[editor_len] = '\0';
        vfs_close(fd);
        snprintf(editor_status, sizeof(editor_status), "Loaded %d bytes", editor_len);
    } else {
        editor_len = 0;
        strcpy(editor_status, "New file");
    }
}

void editor_save(void) {
    int fd = vfs_open(editor_path, O_CREAT | O_TRUNC | O_WRONLY);
    if (fd >= 0) {
        vfs_write(fd, editor_buf, (uint32_t)editor_len);
        vfs_close(fd);
        snprintf(editor_status, sizeof(editor_status), "Saved %d bytes!", editor_len);
    } else {
        strcpy(editor_status, "Save failed!");
    }
}

void editor_key(char c) {
    if (c == '\b') {
        if (editor_len > 0) editor_buf[--editor_len] = '\0';
    } else if ((c == '\n' || (c >= 32 && c <= 126)) && editor_len < (int)sizeof(editor_buf) - 2) {
        editor_buf[editor_len++] = c;
        editor_buf[editor_len] = '\0';
    }
}

void editor_draw(int32_t x, int32_t y, int32_t w, int32_t h) {
    fb_fill_rect(x, y, w, h, RGB(18, 22, 30));
    fb_fill_rect(x + 8, y + 6, w - 16, 22, RGB(30, 38, 52));
    fb_draw_string(x + 14, y + 13, editor_path, RGB(170, 220, 255), 0, true);
    fb_fill_rect(x + w - 72, y + 8, 60, 18, RGB(45, 115, 75));
    fb_draw_string(x + w - 62, y + 13, "[Save]", RGB(240, 255, 245), 0, true);

    int32_t cx = x + 12, cy = y + 36;
    for (int i = 0; i < editor_len && cy + 12 < y + h - 22; i++) {
        char ch = editor_buf[i];
        if (ch == '\n') { cx = x + 12; cy += 12; continue; }
        if (ch >= 32 && ch <= 126) {
            fb_draw_char(cx, cy, ch, RGB(230, 235, 245), 0, true);
            cx += 8;
            if (cx + 12 >= x + w - 8) { cx = x + 12; cy += 12; }
        }
    }
    if ((timer_ticks() / 30) % 2 == 0) {
        fb_fill_rect(cx, cy, 7, 10, RGB(110, 220, 255));
    }
    fb_draw_string(x + 12, y + h - 16, editor_status, RGB(140, 210, 160), 0, true);
}

void editor_click(int32_t rx, int32_t ry, int32_t w) {
    if (rx >= w - 72 && rx <= w - 12 && ry >= 8 && ry <= 26) {
        editor_save();
    }
}

char anv_out_buf[1024];
int  anv_out_len = 0;
char anv_title_path[128] = "";
int  anv_last_rc = 0;

static void anv_popup_cb(const char* text, uint32_t len) {
    for (uint32_t i = 0; i < len && anv_out_len < (int)sizeof(anv_out_buf) - 1; i++) {
        anv_out_buf[anv_out_len++] = text[i];
    }
    anv_out_buf[anv_out_len] = '\0';
}

void anv_runner_exec(const char* path) {
    strncpy(anv_title_path, path, sizeof(anv_title_path) - 1);
    anv_out_len = 0;
    anv_out_buf[0] = '\0';
    int rc = 0;
    int err = anv_exec_file(path, anv_popup_cb, &rc);
    if (err != 0) {
        snprintf(anv_out_buf, sizeof(anv_out_buf), "Failed to execute %s (err=%d)\n", path, err);
    } else {
        anv_last_rc = rc;
    }
}

void anv_runner_draw(int32_t x, int32_t y, int32_t w, int32_t h) {
    fb_fill_rect(x, y, w, h, RGB(14, 18, 26));
    char hdr[160];
    snprintf(hdr, sizeof(hdr), "ANV1 Binary: %s (exit=%d)", anv_title_path, anv_last_rc);
    fb_draw_string(x + 12, y + 10, hdr, RGB(120, 245, 160), 0, true);
    fb_fill_rect(x + 10, y + 26, w - 20, 1, RGB(50, 70, 95));

    int32_t cx = x + 12, cy = y + 34;
    for (int i = 0; anv_out_buf[i] && cy + 12 < y + h - 8; i++) {
        char ch = anv_out_buf[i];
        if (ch == '\n') { cx = x + 12; cy += 12; continue; }
        if (ch >= 32 && ch <= 126) {
            fb_draw_char(cx, cy, ch, RGB(230, 240, 250), 0, true);
            cx += 8;
            if (cx + 12 >= x + w - 8) { cx = x + 12; cy += 12; }
        }
    }
}
