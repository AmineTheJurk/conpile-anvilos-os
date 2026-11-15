#include "gui.h"
#include "fb.h"
#include "keyboard.h"
#include "mouse.h"
#include "serial.h"
#include "timer.h"
#include "rtc.h"
#include "wifi.h"
#include "anvilos_icon.h"
#include "anvilfs.h"
#include "string.h"
#include "io.h"

extern const uint16_t icon_terminal[16];
extern const uint16_t icon_files[16];
extern const uint16_t icon_sysinfo[16];
extern const uint16_t icon_editor[16];
extern const uint16_t icon_anv[16];
extern char fm_cwd[128];
void fm_refresh(void);
void fm_draw(int32_t x, int32_t y, int32_t w, int32_t h);
void fm_click(int32_t rx, int32_t ry, int32_t w);
void sysinfo_draw(int32_t x, int32_t y, int32_t w, int32_t h);
void editor_load(const char* path);
void editor_key(char c);
void editor_draw(int32_t x, int32_t y, int32_t w, int32_t h);
void editor_click(int32_t rx, int32_t ry, int32_t w);
void anv_runner_exec(const char* path);
void anv_runner_draw(int32_t x, int32_t y, int32_t w, int32_t h);

#define TERM_COLS 66
#define TERM_ROWS 24
static char term_lines[TERM_ROWS][TERM_COLS + 1];
static int  term_cur_row = 0;
static int  term_cur_col = 0;

static void term_scroll(void) {
    for (int r = 0; r < TERM_ROWS - 1; r++) {
        memcpy(term_lines[r], term_lines[r + 1], TERM_COLS + 1);
    }
    memset(term_lines[TERM_ROWS - 1], 0, TERM_COLS + 1);
    term_cur_row = TERM_ROWS - 1;
}

void gui_term_putchar(char c) {
    if (c == '\b') {
        if (term_cur_col > 0) {
            term_cur_col--;
            term_lines[term_cur_row][term_cur_col] = '\0';
        }
        return;
    }
    if (c == '\r') return;
    if (c == '\n') {
        term_cur_col = 0;
        term_cur_row++;
        if (term_cur_row >= TERM_ROWS) term_scroll();
        return;
    }
    if (term_cur_col >= TERM_COLS) {
        term_cur_col = 0;
        term_cur_row++;
        if (term_cur_row >= TERM_ROWS) term_scroll();
    }
    term_lines[term_cur_row][term_cur_col++] = c;
    term_lines[term_cur_row][term_cur_col] = '\0';
}

void gui_term_print(const char* s) {
    while (s && *s) gui_term_putchar(*s++);
}

static void term_draw(int32_t x, int32_t y, int32_t w, int32_t h) {
    fb_fill_rect(x, y, w, h, RGB(11, 15, 23));
    for (int r = 0; r < TERM_ROWS; r++) {
        int32_t ry = y + 8 + r * 12;
        if (ry + 10 >= y + h) break;
        fb_draw_string(x + 8, ry, term_lines[r], RGB(215, 235, 245), 0, true);
    }
    if ((timer_ticks() / 30) % 2 == 0) {
        int32_t cx = x + 8 + term_cur_col * 8;
        int32_t cy = y + 8 + term_cur_row * 12;
        if (cy + 10 < y + h && cx + 8 < x + w) {
            fb_fill_rect(cx, cy, 7, 10, RGB(100, 225, 175));
        }
    }
}

typedef enum {
    WIN_TERM = 0,
    WIN_FILES,
    WIN_SYSINFO,
    WIN_EDITOR,
    WIN_ANVRUN,
    WIN_COUNT
} win_id_t;

typedef struct {
    win_id_t id;
    const char* title;
    int32_t x, y, w, h;
    bool open;
} window_t;

static window_t windows[WIN_COUNT] = {
    { WIN_TERM,    "Terminal (ash)",       210, 48,  550, 324, true },
    { WIN_FILES,   "AnvilFS File Manager", 230, 96,  440, 300, false },
    { WIN_SYSINFO, "System Monitor",       360, 120, 460, 290, false },
    { WIN_EDITOR,  "AnvilPad Editor",      210, 140, 480, 300, false },
    { WIN_ANVRUN,  "ANV Binary Output",    280, 170, 440, 220, false },
};
static int z_order[WIN_COUNT] = { WIN_FILES, WIN_SYSINFO, WIN_EDITOR, WIN_ANVRUN, WIN_TERM };

typedef struct {
    const char* label;
    const uint16_t* icon;
    uint32_t color;
    int32_t x, y;
    int action;
} desktop_icon_t;

static const desktop_icon_t icons[] = {
    { "Terminal",    icon_terminal, RGB(95, 215, 175),  24, 24,  0 },
    { "Files",       icon_files,    RGB(115, 190, 255), 24, 110, 1 },
    { "System Info", icon_sysinfo,  RGB(250, 195, 95),  24, 196, 2 },
    { "AnvilPad",    icon_editor,   RGB(215, 165, 255), 24, 282, 3 },
    { "hello.anv",   icon_anv,      RGB(130, 245, 150), 24, 368, 4 },
    { "sysinfo.anv", icon_anv,      RGB(130, 245, 150), 24, 454, 5 },
};
#define NUM_ICONS ((int)(sizeof(icons) / sizeof(icons[0])))

static int  selected_icon   = -1;
static bool start_menu_open = false;

/* Wi-Fi popup state (opened by clicking the taskbar Wi-Fi icon next to Date/Time) */
static bool wifi_menu_open  = false;
static int  wifi_sel_idx    = 0;
static char wifi_pass[40]   = "";
static int  wifi_pass_len   = 0;
static char wifi_msg[64]    = "";

static void bring_to_front(win_id_t id) {
    int pos = -1;
    for (int i = 0; i < WIN_COUNT; i++) {
        if (z_order[i] == (int)id) { pos = i; break; }
    }
    if (pos >= 0) {
        for (int i = pos; i < WIN_COUNT - 1; i++) z_order[i] = z_order[i + 1];
        z_order[WIN_COUNT - 1] = (int)id;
    }
}

void gui_open_terminal(void) { windows[WIN_TERM].open = true; bring_to_front(WIN_TERM); }
void gui_open_files(const char* path) {
    if (path) strncpy(fm_cwd, path, sizeof(fm_cwd) - 1);
    fm_refresh();
    windows[WIN_FILES].open = true;
    bring_to_front(WIN_FILES);
}
void gui_open_sysinfo(void) { windows[WIN_SYSINFO].open = true; bring_to_front(WIN_SYSINFO); }
void gui_open_editor(const char* path) {
    editor_load(path ? path : "/home/user/readme.txt");
    windows[WIN_EDITOR].open = true;
    bring_to_front(WIN_EDITOR);
}
void gui_run_anv_popup(const char* path) {
    anv_runner_exec(path);
    windows[WIN_ANVRUN].open = true;
    bring_to_front(WIN_ANVRUN);
}

static void trigger_icon(int idx) {
    switch (idx) {
        case 0: gui_open_terminal(); break;
        case 1: gui_open_files("/"); break;
        case 2: gui_open_sysinfo(); break;
        case 3: gui_open_editor("/home/user/readme.txt"); break;
        case 4: gui_run_anv_popup("/bin/hello.anv"); break;
        case 5: gui_run_anv_popup("/bin/sysinfo.anv"); break;
    }
}

static void draw_wifi_icon(int32_t x, int32_t y, bool connected, uint8_t pct, uint32_t col) {
    /* Crisp Wi-Fi waves (3 arcs + base dot) at 16x16 footprint */
    int bars = connected ? (pct >= 75 ? 3 : (pct >= 45 ? 2 : 1)) : 3;
    uint32_t c3 = (bars >= 3) ? col : RGB(75, 92, 118);
    uint32_t c2 = (bars >= 2) ? col : RGB(75, 92, 118);
    uint32_t c1 = (bars >= 1) ? col : RGB(75, 92, 118);

    /* Outer wave */
    fb_draw_line(x + 2, y + 4, x + 5, y + 2, c3);
    fb_draw_line(x + 5, y + 2, x + 10, y + 2, c3);
    fb_draw_line(x + 10, y + 2, x + 13, y + 4, c3);
    /* Middle wave */
    fb_draw_line(x + 4, y + 7, x + 6, y + 5, c2);
    fb_draw_line(x + 6, y + 5, x + 9, y + 5, c2);
    fb_draw_line(x + 9, y + 5, x + 11, y + 7, c2);
    /* Inner wave */
    fb_draw_line(x + 6, y + 10, x + 7, y + 9, c1);
    fb_draw_line(x + 8, y + 9, x + 9, y + 10, c1);
    /* Base dot */
    fb_fill_rect(x + 7, y + 12, 2, 2, connected ? RGB(105, 245, 165) : col);
}

static void draw_signal_bars(int32_t x, int32_t y, uint8_t pct, uint32_t col) {
    int n = (pct >= 80) ? 4 : (pct >= 60 ? 3 : (pct >= 35 ? 2 : 1));
    for (int b = 0; b < 4; b++) {
        int32_t bh = 3 + b * 3;
        uint32_t bc = (b < n) ? col : RGB(55, 68, 88);
        fb_fill_rect(x + b * 4, y + (12 - bh), 3, bh, bc);
    }
}

static void draw_cursor(int32_t mx, int32_t my) {
    static const uint16_t cur[12] = {
        0x8000,0xC000,0xE000,0xF000,
        0xF800,0xFC00,0xFE00,0xFF00,
        0xF800,0xD800,0x8C00,0x0C00
    };
    for (int r = 0; r < 12; r++) {
        for (int c = 0; c < 12; c++) {
            if (cur[r] & (0x8000 >> c)) {
                fb_put_pixel(mx + c + 1, my + r + 1, RGB(10, 15, 25));
                fb_put_pixel(mx + c, my + r, RGB(255, 255, 255));
            }
        }
    }
}

static void render_wifi_popup(int32_t sw, int32_t tby) {
    int32_t pw = 296, ph = 290;
    int32_t px = sw - pw - 8;
    int32_t py = tby - ph - 4;

    fb_fill_rect(px + 4, py + 4, pw, ph, RGB(5, 8, 13));
    fb_fill_rect(px, py, pw, ph, RGB(20, 27, 40));
    fb_draw_rect(px, py, pw, ph, RGB(88, 166, 255));

    /* Title bar + Scan button */
    fb_fill_rect(px, py, pw, 28, RGB(30, 48, 78));
    fb_draw_string(px + 10, py + 10, "Wi-Fi Networks", RGB(240, 248, 255), 0, true);
    fb_fill_rect(px + pw - 58, py + 5, 50, 18, RGB(45, 92, 162));
    fb_draw_string(px + pw - 49, py + 10, "Scan", RGB(255, 255, 255), 0, true);

    /* Hardware/Connection status line */
    fb_draw_string(px + 10, py + 36, wifi_hw_label(), RGB(140, 195, 245), 0, true);

    int cnt = wifi_bss_count();
    if (wifi_sel_idx >= cnt) wifi_sel_idx = cnt - 1;
    if (wifi_sel_idx < 0) wifi_sel_idx = 0;

    int32_t ly = py + 52;
    for (int i = 0; i < cnt && i < 6; i++) {
        const wifi_bss_t* b = wifi_get_bss(i);
        bool sel = (i == wifi_sel_idx);
        uint32_t rbg = b->connected ? RGB(28, 78, 62) : (sel ? RGB(38, 62, 102) : RGB(26, 35, 52));
        fb_fill_rect(px + 8, ly, pw - 16, 24, rbg);
        if (sel) fb_draw_rect(px + 8, ly, pw - 16, 24, RGB(105, 175, 255));

        draw_signal_bars(px + 14, ly + 6, b->signal_pct,
                         b->connected ? RGB(105, 245, 165) : RGB(150, 205, 255));
        fb_draw_string(px + 34, ly + 8, b->ssid, RGB(235, 245, 255), 0, true);

        const char* tag = b->connected ? "Active" : (b->secured ? "WPA2" : "Open");
        uint32_t tcol = b->connected ? RGB(115, 250, 170) : RGB(165, 180, 205);
        fb_draw_string(px + pw - 62, ly + 8, tag, tcol, 0, true);
        ly += 27;
    }

    /* Passphrase box + Connect / Disconnect buttons */
    const wifi_bss_t* sb = wifi_get_bss(wifi_sel_idx);
    int32_t fy = py + ph - 68;
    fb_draw_string(px + 10, fy, (sb && sb->secured) ? "Passphrase:" : "Open Network (No Password)",
                   RGB(180, 200, 230), 0, true);
    fb_fill_rect(px + 10, fy + 12, pw - 20, 20, RGB(11, 15, 22));
    fb_draw_rect(px + 10, fy + 12, pw - 20, 20, RGB(70, 95, 130));
    if (sb && sb->secured) {
        char masked[42];
        int ml = wifi_pass_len > 30 ? 30 : wifi_pass_len;
        for (int i = 0; i < ml; i++) masked[i] = '*';
        masked[ml] = '\0';
        fb_draw_string(px + 16, fy + 18, ml > 0 ? masked : "(type password...)",
                       ml > 0 ? RGB(235, 245, 255) : RGB(95, 115, 145), 0, true);
    }

    int32_t by = py + ph - 30;
    bool sel_connected = (sb && sb->connected);
    fb_fill_rect(px + 10, by, 130, 22, sel_connected ? RGB(155, 55, 65) : RGB(42, 135, 88));
    fb_draw_string(px + 36, by + 7, sel_connected ? "Disconnect" : "Connect",
                   RGB(255, 255, 255), 0, true);

    if (wifi_msg[0]) {
        fb_draw_string(px + 148, by + 7, wifi_msg, RGB(245, 210, 120), 0, true);
    } else if (wifi_is_connected()) {
        fb_draw_string(px + 148, by + 7, "IP: 10.0.2.15", RGB(125, 235, 175), 0, true);
    }
}

static void render_frame(int32_t mx, int32_t my) {
    uint32_t sw = fb_width();
    uint32_t sh = fb_height();
    uint32_t tb_h = 34;

    fb_fill_gradient_v(0, 0, (int32_t)sw, (int32_t)(sh - tb_h), RGB(14, 22, 38), RGB(26, 44, 72));
    fb_draw_string((int32_t)sw - 220, 14, "AnvilOS 1.0 (AnvilFS)", RGB(75, 105, 145), 0, true);

    for (int i = 0; i < NUM_ICONS; i++) {
        int32_t ix = icons[i].x, iy = icons[i].y;
        if (selected_icon == i) {
            fb_fill_rect(ix - 6, iy - 6, 86, 68, RGB(42, 72, 118));
            fb_draw_rect(ix - 6, iy - 6, 86, 68, RGB(110, 175, 245));
        }
        fb_draw_bitmap_16x16(ix + 16, iy, icons[i].icon, icons[i].color, 2);
        int lbl_len = (int)strlen(icons[i].label);
        int32_t lx = ix + 32 - (lbl_len * 4);
        if (lx < ix - 4) lx = ix - 4;
        fb_draw_string(lx, iy + 38, icons[i].label, RGB(235, 242, 252), 0, true);
    }

    int top_id = z_order[WIN_COUNT - 1];
    for (int zi = 0; zi < WIN_COUNT; zi++) {
        window_t* w = &windows[z_order[zi]];
        if (!w->open) continue;
        bool active = ((int)w->id == top_id);

        fb_fill_rect(w->x + 3, w->y + 3, w->w, w->h + 24, RGB(6, 9, 14));
        fb_draw_rect(w->x - 1, w->y - 1, w->w + 2, w->h + 26, active ? RGB(88, 166, 255) : RGB(55, 70, 95));

        fb_fill_rect(w->x, w->y, w->w, 24, active ? RGB(33, 52, 84) : RGB(26, 34, 48));
        fb_draw_string(w->x + 10, w->y + 8, w->title, RGB(240, 245, 255), 0, true);

        fb_fill_rect(w->x + w->w - 22, w->y + 3, 18, 18, RGB(175, 55, 65));
        fb_draw_char(w->x + w->w - 17, w->y + 8, 'X', RGB(255, 255, 255), 0, true);

        int32_t cx = w->x, cy = w->y + 24;
        switch (w->id) {
            case WIN_TERM:    term_draw(cx, cy, w->w, w->h); break;
            case WIN_FILES:   fm_draw(cx, cy, w->w, w->h); break;
            case WIN_SYSINFO: sysinfo_draw(cx, cy, w->w, w->h); break;
            case WIN_EDITOR:  editor_draw(cx, cy, w->w, w->h); break;
            case WIN_ANVRUN:  anv_runner_draw(cx, cy, w->w, w->h); break;
            default: break;
        }
    }

    int32_t tby = (int32_t)(sh - tb_h);

    if (start_menu_open) {
        int32_t sx = 6, sy = tby - 200;
        fb_fill_rect(sx, sy, 200, 196, RGB(22, 30, 44));
        fb_draw_rect(sx, sy, 200, 196, RGB(88, 166, 255));

        /* AnvilOS Start Menu Header with user-supplied 32x32 AnvilOS icon */
        fb_fill_rect(sx + 2, sy + 2, 196, 40, RGB(28, 44, 72));
        fb_fill_rect(sx + 8, sy + 6, 32, 32, RGB(242, 247, 255));
        fb_draw_argb_icon(sx + 8, sy + 6, ANVIL_ICON_LG_W, ANVIL_ICON_LG_H, anvilos_icon_lg);
        fb_draw_string(sx + 48, sy + 18, "AnvilOS 1.0", RGB(240, 248, 255), 0, true);

        const char* items[5] = {
            "Terminal (ash)", "Files (AnvilFS)", "System Info",
            "AnvilPad Editor", "Run hello.anv"
        };
        for (int i = 0; i < 5; i++) {
            fb_fill_rect(sx + 4, sy + 46 + i * 30, 192, 26, RGB(30, 42, 62));
            fb_draw_string(sx + 12, sy + 55 + i * 30, items[i], RGB(230, 240, 252), 0, true);
        }
    }

    if (wifi_menu_open) {
        render_wifi_popup((int32_t)sw, tby);
    }

    /* Taskbar background */
    fb_fill_rect(0, tby, (int32_t)sw, (int32_t)tb_h, RGB(16, 22, 34));
    fb_fill_rect(0, tby, (int32_t)sw, 1, RGB(60, 82, 115));

    /* Taskbar Left: User-supplied AnvilOS icon inside Start Button */
    fb_fill_rect(6, tby + 3, 102, 28, start_menu_open ? RGB(60, 120, 210) : RGB(34, 56, 94));
    fb_draw_rect(6, tby + 3, 102, 28, RGB(88, 152, 240));
    fb_fill_rect(9, tby + 5, 24, 24, RGB(242, 247, 255));
    fb_draw_argb_icon(10, tby + 6, ANVIL_ICON_TB_W, ANVIL_ICON_TB_H, anvilos_icon_tb);
    fb_draw_string(39, tby + 13, "AnvilOS", RGB(240, 248, 255), 0, true);

    /* Open Window Tabs */
    int32_t tab_x = 116;
    int32_t tray_w = 214;
    for (int i = 0; i < WIN_COUNT; i++) {
        if (!windows[i].open) continue;
        if (tab_x + 120 > (int32_t)sw - tray_w) break;
        bool active = (z_order[WIN_COUNT - 1] == i);
        fb_fill_rect(tab_x, tby + 4, 124, 26, active ? RGB(45, 76, 124) : RGB(26, 36, 52));
        fb_draw_string(tab_x + 6, tby + 13, windows[i].title, RGB(220, 230, 245), 0, true);
        tab_x += 128;
    }

    /* Taskbar Right Tray: Wi-Fi Icon + Date & Time */
    int32_t wifi_bx = (int32_t)sw - 196;
    fb_fill_rect(wifi_bx, tby + 4, 34, 26, wifi_menu_open ? RGB(52, 92, 154) : RGB(24, 34, 50));
    fb_draw_rect(wifi_bx, tby + 4, 34, 26, wifi_is_connected() ? RGB(75, 195, 140) : RGB(58, 78, 110));
    draw_wifi_icon(wifi_bx + 9, tby + 9, wifi_is_connected(), wifi_signal_pct(),
                   wifi_is_connected() ? RGB(105, 245, 165) : RGB(190, 215, 245));

    rtc_datetime_t dt;
    rtc_read(&dt);
    char dt_buf[32];
    snprintf(dt_buf, sizeof(dt_buf), "%02u:%02u %02u/%02u/%04u",
             (uint32_t)dt.hour, (uint32_t)dt.minute,
             (uint32_t)dt.month, (uint32_t)dt.day, (uint32_t)dt.year);
    fb_fill_rect((int32_t)sw - 156, tby + 4, 150, 26, RGB(22, 30, 44));
    fb_draw_string((int32_t)sw - 146, tby + 13, dt_buf, RGB(185, 215, 245), 0, true);

    draw_cursor(mx, my);
    fb_present();
}

void gui_run(void) {
    if (!fb_is_active()) {
        serial_puts("[GUI] No VBE framebuffer; running interactive serial shell.\n");
        for (;;) {
            if (serial_has_char()) {
                ash_handle_char(serial_getc());
            } else if (keyboard_has_char()) {
                ash_handle_char(keyboard_getc());
            } else {
                hlt();
            }
        }
    }

    fm_refresh();
    editor_load("/home/user/readme.txt");

    mouse_state_t ms;
    mouse_get_state(&ms);
    uint8_t prev_btns = 0;
    int dragging_win = -1;
    int32_t drag_ox = 0, drag_oy = 0;
    uint32_t last_tick = 0;
    bool dirty = true;

    for (;;) {
        bool had_input = false;

        while (keyboard_has_char()) {
            char c = keyboard_getc();
            if (wifi_menu_open) {
                if (c == 27) { /* ESC */
                    wifi_menu_open = false;
                } else if (c == '\b' || c == 127) {
                    if (wifi_pass_len > 0) wifi_pass[--wifi_pass_len] = '\0';
                } else if (c == '\n' || c == '\r') {
                    if (wifi_connect(wifi_sel_idx, wifi_pass)) {
                        snprintf(wifi_msg, sizeof(wifi_msg), "Connected!");
                    } else {
                        snprintf(wifi_msg, sizeof(wifi_msg), "Need pass!");
                    }
                } else if (c >= 32 && c <= 126 && wifi_pass_len < (int)sizeof(wifi_pass) - 1) {
                    wifi_pass[wifi_pass_len++] = c;
                    wifi_pass[wifi_pass_len] = '\0';
                }
            } else if (z_order[WIN_COUNT - 1] == WIN_EDITOR && windows[WIN_EDITOR].open) {
                editor_key(c);
            } else {
                ash_handle_char(c);
            }
            dirty = true;
            had_input = true;
        }
        while (serial_has_char()) {
            ash_handle_char(serial_getc());
            dirty = true;
            had_input = true;
        }

        mouse_get_state(&ms);
        if (ms.moved) dirty = true;

        bool left_down = (ms.buttons & 0x01) != 0;
        bool left_prev = (prev_btns & 0x01) != 0;
        bool left_click = ((ms.clicked & 0x01) != 0) || (left_down && !left_prev);

        if (!left_down) dragging_win = -1;
        if (dragging_win >= 0 && ms.moved) {
            windows[dragging_win].x = ms.x - drag_ox;
            windows[dragging_win].y = ms.y - drag_oy;
            if (windows[dragging_win].y < 0) windows[dragging_win].y = 0;
            dirty = true;
        }

        if (left_click) {
            dirty = true;
            uint32_t sw = fb_width(), sh = fb_height();
            int32_t tby = (int32_t)sh - 34;

            if (wifi_menu_open) {
                int32_t pw = 296, ph = 290;
                int32_t px = (int32_t)sw - pw - 8;
                int32_t py = tby - ph - 4;
                if (ms.x >= px && ms.x < px + pw && ms.y >= py && ms.y < py + ph) {
                    /* Scan button */
                    if (ms.x >= px + pw - 58 && ms.x <= px + pw - 8 && ms.y >= py + 5 && ms.y <= py + 23) {
                        wifi_scan();
                        snprintf(wifi_msg, sizeof(wifi_msg), "Scanned!");
                    }
                    /* BSS network rows */
                    int32_t ly = py + 52;
                    int cnt = wifi_bss_count();
                    for (int i = 0; i < cnt && i < 6; i++) {
                        if (ms.y >= ly && ms.y < ly + 24 && ms.x >= px + 8 && ms.x < px + pw - 8) {
                            wifi_sel_idx = i;
                            wifi_msg[0] = '\0';
                            break;
                        }
                        ly += 27;
                    }
                    /* Connect / Disconnect button */
                    int32_t by = py + ph - 30;
                    if (ms.x >= px + 10 && ms.x <= px + 140 && ms.y >= by && ms.y <= by + 22) {
                        const wifi_bss_t* sb = wifi_get_bss(wifi_sel_idx);
                        if (sb && sb->connected) {
                            wifi_disconnect();
                            snprintf(wifi_msg, sizeof(wifi_msg), "Disconnected");
                        } else if (wifi_connect(wifi_sel_idx, wifi_pass)) {
                            snprintf(wifi_msg, sizeof(wifi_msg), "Connected!");
                        } else {
                            snprintf(wifi_msg, sizeof(wifi_msg), "Need pass!");
                        }
                    }
                    prev_btns = ms.buttons;
                    continue;
                }
                wifi_menu_open = false;
            }

            if (start_menu_open) {
                int32_t sx = 6, sy = tby - 200;
                if (ms.x >= sx && ms.x < sx + 200 && ms.y >= sy && ms.y < sy + 196) {
                    if (ms.y >= sy + 46) {
                        int item = (ms.y - (sy + 46)) / 30;
                        start_menu_open = false;
                        if (item >= 0 && item < 5) trigger_icon(item);
                    } else {
                        start_menu_open = false;
                    }
                    prev_btns = ms.buttons;
                    continue;
                }
                start_menu_open = false;
            }

            if (ms.y >= tby) {
                int32_t wifi_bx = (int32_t)sw - 196;
                if (ms.x >= 6 && ms.x <= 108) {
                    start_menu_open = !start_menu_open;
                    wifi_menu_open = false;
                } else if (ms.x >= wifi_bx && ms.x <= wifi_bx + 34) {
                    wifi_menu_open = !wifi_menu_open;
                    start_menu_open = false;
                    wifi_msg[0] = '\0';
                } else {
                    int32_t tab_x = 116;
                    int32_t tray_w = 214;
                    for (int i = 0; i < WIN_COUNT; i++) {
                        if (!windows[i].open) continue;
                        if (tab_x + 120 > (int32_t)sw - tray_w) break;
                        if (ms.x >= tab_x && ms.x < tab_x + 124) {
                            bring_to_front((win_id_t)i);
                            break;
                        }
                        tab_x += 128;
                    }
                }
                prev_btns = ms.buttons;
                continue;
            }

            bool hit_win = false;
            for (int zi = WIN_COUNT - 1; zi >= 0; zi--) {
                window_t* w = &windows[z_order[zi]];
                if (!w->open) continue;
                if (ms.x >= w->x && ms.x < w->x + w->w && ms.y >= w->y && ms.y < w->y + 24 + w->h) {
                    bring_to_front(w->id);
                    hit_win = true;
                    if (ms.y < w->y + 24) {
                        if (ms.x >= w->x + w->w - 22) {
                            w->open = false;
                        } else {
                            dragging_win = (int)w->id;
                            drag_ox = ms.x - w->x;
                            drag_oy = ms.y - w->y;
                        }
                    } else {
                        int32_t rx = ms.x - w->x;
                        int32_t ry = ms.y - (w->y + 24);
                        if (w->id == WIN_FILES)  fm_click(rx, ry, w->w);
                        if (w->id == WIN_EDITOR) editor_click(rx, ry, w->w);
                    }
                    break;
                }
            }

            if (!hit_win) {
                int clicked_icon = -1;
                for (int i = 0; i < NUM_ICONS; i++) {
                    if (ms.x >= icons[i].x - 6 && ms.x < icons[i].x + 80 &&
                        ms.y >= icons[i].y - 6 && ms.y < icons[i].y + 62) {
                        clicked_icon = i;
                        break;
                    }
                }
                if (clicked_icon >= 0) {
                    if (selected_icon == clicked_icon) {
                        trigger_icon(clicked_icon);
                    } else {
                        selected_icon = clicked_icon;
                    }
                } else {
                    selected_icon = -1;
                }
            }
        }
        prev_btns = ms.buttons;

        uint32_t now = timer_ticks();
        uint32_t interval = 25;
        if (now - last_tick >= interval) {
            last_tick = now;
            dirty = true;
        }

        if (dirty) {
            render_frame(ms.x, ms.y);
            dirty = false;
        } else if (!had_input) {
            hlt();
        }
    }
}
