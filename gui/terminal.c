#include "gui.h"
#include "vfs.h"
#include "anvilfs.h"
#include "anv.h"
#include "task.h"
#include "pci.h"
#include "mem.h"
#include "timer.h"
#include "ata.h"
#include "kmod.h"
#include "wifi.h"
#include "rtc.h"
#include "acpi.h"
#include "io.h"
#include "serial.h"
#include "string.h"

static char cwd[128] = "/home/user";
static char cmd_buf[128];
static int  cmd_len = 0;

static void term_out(const char* s) {
    serial_puts(s);
    gui_term_print(s);
}

static void term_anv_cb(const char* text, uint32_t len) {
    char tmp[129];
    uint32_t off = 0;
    while (off < len) {
        uint32_t chunk = len - off;
        if (chunk > 128) chunk = 128;
        memcpy(tmp, text + off, chunk);
        tmp[chunk] = '\0';
        gui_term_print(tmp);
        off += chunk;
    }
}

static void print_prompt(void) {
    char p[160];
    snprintf(p, sizeof(p), "root@anvilos:%s$ ", cwd);
    term_out(p);
}

void ash_get_cwd(char* out, size_t sz) {
    strncpy(out, cwd, sz - 1);
    out[sz - 1] = '\0';
}

void ash_init(void) {
    strcpy(cwd, "/home/user");
    cmd_len = 0;
    cmd_buf[0] = '\0';
    term_out("AnvilOS Unix-like Shell (ash 1.0)\n");
    term_out("Type 'help' for commands, or '/bin/hello.anv' to run a compiled binary.\n\n");
    print_prompt();
}

static void cmd_ls(const char* arg) {
    char path[128];
    vfs_normalize_path(cwd, (arg && *arg) ? arg : ".", path, sizeof(path));
    anvilfs_dirent_t* ents = (anvilfs_dirent_t*)kmalloc(sizeof(anvilfs_dirent_t) * 128);
    if (!ents) { term_out("ls: OOM\n"); return; }
    int n = vfs_readdir(path, ents, 128);
    if (n < 0) {
        kfree(ents);
        term_out("ls: cannot open directory\n");
        return;
    }
    char line[128];
    for (int i = 0; i < n; i++) {
        char child[160];
        if (strcmp(path, "/") == 0) snprintf(child, sizeof(child), "/%s", ents[i].name);
        else snprintf(child, sizeof(child), "%s/%s", path, ents[i].name);
        vfs_stat_t st;
        if (vfs_stat(child, &st)) {
            if (st.type == ANVILFS_TYPE_DIR) {
                snprintf(line, sizeof(line), "drwxr-xr-x  <DIR>     %s/\n", ents[i].name);
            } else {
                snprintf(line, sizeof(line), "-rw-r--r--  %7uB  %s\n", st.size, ents[i].name);
            }
            term_out(line);
        }
    }
    kfree(ents);
}

static void cmd_cat(const char* arg) {
    if (!arg || !*arg) { term_out("usage: cat <file>\n"); return; }
    char path[128];
    vfs_normalize_path(cwd, arg, path, sizeof(path));
    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) { term_out("cat: file not found\n"); return; }
    char buf[257];
    int n;
    while ((n = vfs_read(fd, buf, 256)) > 0) {
        buf[n] = '\0';
        term_out(buf);
    }
    vfs_close(fd);
    term_out("\n");
}

static void cmd_run_anv(const char* arg) {
    char path[128];
    vfs_normalize_path(cwd, arg, path, sizeof(path));
    int rc = 0;
    int err = anv_exec_file(path, term_anv_cb, &rc);
    if (err != 0) {
        char msg[96];
        snprintf(msg, sizeof(msg), "exec failed (%d): %s\n", err, path);
        term_out(msg);
    } else {
        char msg[64];
        snprintf(msg, sizeof(msg), "[exited with code %d]\n", rc);
        term_out(msg);
    }
}

void ash_exec_line(const char* line) {
    char buf[128];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char* p = buf;
    while (*p == ' ' || *p == '\t') p++;
    size_t len = strlen(p);
    while (len > 0 && (p[len - 1] == ' ' || p[len - 1] == '\r' || p[len - 1] == '\n')) {
        p[--len] = '\0';
    }
    if (!*p) return;

    char* sp = strchr(p, ' ');
    char* arg = NULL;
    if (sp) {
        *sp = '\0';
        arg = sp + 1;
        while (*arg == ' ') arg++;
    }

    if (strcmp(p, "help") == 0) {
        term_out("Commands: help ls lsmod modprobe rmmod ifconfig wifi date cd pwd\n"
                 "          cat touch write mkdir rm df ps free lspci uname run reboot shutdown\n");
    } else if (strcmp(p, "pwd") == 0) {
        term_out(cwd);
        term_out("\n");
    } else if (strcmp(p, "uname") == 0) {
        term_out("AnvilOS 1.0.0 i686 AnvilKernel (AnvilFS + ANV1)\n");
    } else if (strcmp(p, "ls") == 0) {
        cmd_ls(arg);
    } else if (strcmp(p, "cd") == 0) {
        char path[128];
        vfs_normalize_path(cwd, (arg && *arg) ? arg : "/home/user", path, sizeof(path));
        vfs_stat_t st;
        if (vfs_stat(path, &st) && st.type == ANVILFS_TYPE_DIR) {
            strcpy(cwd, path);
        } else {
            term_out("cd: no such directory\n");
        }
    } else if (strcmp(p, "cat") == 0) {
        cmd_cat(arg);
    } else if (strcmp(p, "touch") == 0) {
        if (!arg || !*arg) { term_out("usage: touch <file>\n"); }
        else {
            char path[128];
            vfs_normalize_path(cwd, arg, path, sizeof(path));
            int fd = vfs_open(path, O_CREAT | O_WRONLY);
            if (fd >= 0) vfs_close(fd);
            else term_out("touch: failed\n");
        }
    } else if (strcmp(p, "write") == 0) {
        if (!arg || !*arg) { term_out("usage: write <file> <text>\n"); }
        else {
            char* sp2 = strchr(arg, ' ');
            if (!sp2) { term_out("usage: write <file> <text>\n"); }
            else {
                *sp2 = '\0';
                const char* text = sp2 + 1;
                char path[128];
                vfs_normalize_path(cwd, arg, path, sizeof(path));
                int fd = vfs_open(path, O_CREAT | O_TRUNC | O_WRONLY);
                if (fd >= 0) {
                    vfs_write(fd, text, (uint32_t)strlen(text));
                    vfs_write(fd, "\n", 1);
                    vfs_close(fd);
                } else {
                    term_out("write: failed\n");
                }
            }
        }
    } else if (strcmp(p, "mkdir") == 0) {
        if (!arg || !*arg) term_out("usage: mkdir <dir>\n");
        else {
            char path[128];
            vfs_normalize_path(cwd, arg, path, sizeof(path));
            if (!vfs_mkdir(path)) term_out("mkdir: failed\n");
        }
    } else if (strcmp(p, "rm") == 0) {
        if (!arg || !*arg) term_out("usage: rm <file>\n");
        else {
            char path[128];
            vfs_normalize_path(cwd, arg, path, sizeof(path));
            if (!vfs_unlink(path)) term_out("rm: failed\n");
        }
    } else if (strcmp(p, "df") == 0) {
        uint32_t tb, fb, ti, fi;
        anvilfs_statfs(&tb, &fb, &ti, &fi);
        char out[128];
        snprintf(out, sizeof(out), "Backend: %s | Blocks: %u/%u used | Inodes: %u/%u free\n",
                 anvilfs_backend_name(), tb - fb, tb, fi, ti);
        term_out(out);
    } else if (strcmp(p, "free") == 0) {
        char out[128];
        snprintf(out, sizeof(out), "RAM: %u/%u KB used | Kernel Heap: %u/%u KB\n",
                 pmm_used_kb(), pmm_total_kb(), kheap_used_bytes() / 1024, kheap_total_bytes() / 1024);
        term_out(out);
    } else if (strcmp(p, "ps") == 0) {
        task_info_t tl[MAX_TASKS];
        int cnt = task_list(tl, MAX_TASKS);
        char out[96];
        for (int i = 0; i < cnt; i++) {
            const char* st = tl[i].state == TASK_RUNNING ? "RUN" : "DONE";
            snprintf(out, sizeof(out), "PID %-3u %-6s %6uB  %s\n", tl[i].pid, st, tl[i].mem_bytes, tl[i].name);
            term_out(out);
        }
    } else if (strcmp(p, "lspci") == 0) {
        int n = pci_device_count();
        char out[128];
        for (int i = 0; i < n; i++) {
            const pci_device_t* d = pci_get_device(i);
            snprintf(out, sizeof(out), "%02x:%02x.%x %04x:%04x %s\n",
                     d->bus, d->slot, d->func, d->vendor_id, d->device_id,
                     pci_class_name(d->class_code, d->subclass));
            term_out(out);
        }
    } else if (strcmp(p, "lsmod") == 0) {
        int mc = kmod_loaded_count();
        char out[160];
        snprintf(out, sizeof(out), "Loaded OpenBSD Modules (%d active in RAM):\n", mc);
        term_out(out);
        for (int i = 0; i < mc; i++) {
            const loaded_mod_info_t* m = kmod_get_loaded(i);
            snprintf(out, sizeof(out), "  %-12s @0x%08x %5uB  [%04x:%04x]  %s\n",
                     m->name, m->load_addr, m->mem_bytes, m->vendor_id, m->device_id, m->origin);
            term_out(out);
        }
        int gc = kmod_gpu_count();
        for (int i = 0; i < gc; i++) {
            const gpu_adapter_info_t* g = kmod_get_gpu(i);
            snprintf(out, sizeof(out), "  [GPU] %-10s %s [%04x] MMIO=0x%08x VRAM=%uMB (%s)\n",
                     g->driver, g->vendor, g->device_id, g->mmio_phys, g->vram_mb, g->note);
            term_out(out);
        }
    } else if (strcmp(p, "modprobe") == 0) {
        if (!arg || !*arg) term_out("usage: modprobe <module_name>\n");
        else if (kmod_load_by_name(arg, NULL)) term_out("modprobe: loaded module cleanly.\n");
        else term_out("modprobe: failed or not found in /lib/modules/.\n");
    } else if (strcmp(p, "rmmod") == 0) {
        if (!arg || !*arg) term_out("usage: rmmod <module_name>\n");
        else if (kmod_unload_by_name(arg)) term_out("rmmod: unloaded module.\n");
        else term_out("rmmod: module not loaded.\n");
    } else if (strcmp(p, "ifconfig") == 0) {
        int nc = kmod_net_count();
        if (nc == 0) {
            term_out("No active network/Wi-Fi interfaces.\n");
        } else {
            char out[128];
            for (int i = 0; i < nc; i++) {
                const net_iface_info_t* ni = kmod_get_net(i);
                snprintf(out, sizeof(out),
                         "%s: <%s> drv=%s MAC=%02x:%02x:%02x:%02x:%02x:%02x speed=%uMbps base=0x%x\n",
                         ni->iface, ni->link_up ? "UP" : "DOWN", ni->driver,
                         ni->mac[0], ni->mac[1], ni->mac[2], ni->mac[3], ni->mac[4], ni->mac[5],
                         ni->speed_mbps, ni->base_addr);
                term_out(out);
            }
        }
    } else if (strcmp(p, "date") == 0) {
        rtc_datetime_t dt;
        rtc_read(&dt);
        char out[64];
        snprintf(out, sizeof(out), "%04u-%02u-%02u %02u:%02u:%02u UTC\n",
                 (uint32_t)dt.year, (uint32_t)dt.month, (uint32_t)dt.day,
                 (uint32_t)dt.hour, (uint32_t)dt.minute, (uint32_t)dt.second);
        term_out(out);
    } else if (strcmp(p, "wifi") == 0) {
        if (arg && strcmp(arg, "scan") == 0) {
            int n = wifi_scan();
            char out[96];
            snprintf(out, sizeof(out), "Wi-Fi [%s] scanned %d networks:\n", wifi_hw_label(), n);
            term_out(out);
            for (int i = 0; i < n; i++) {
                const wifi_bss_t* b = wifi_get_bss(i);
                snprintf(out, sizeof(out), "  %-16s %3u%% ch%-2u %-4s %s\n",
                         b->ssid, (uint32_t)b->signal_pct, (uint32_t)b->channel,
                         b->secured ? "WPA2" : "OPEN", b->connected ? "[CONNECTED]" : "");
                term_out(out);
            }
        } else if (arg && strncmp(arg, "connect ", 8) == 0) {
            const char* rest = arg + 8;
            char ssid[32];
            char pass[48];
            pass[0] = '\0';
            const char* sp2 = strchr(rest, ' ');
            if (sp2) {
                size_t sl = (size_t)(sp2 - rest);
                if (sl >= sizeof(ssid)) sl = sizeof(ssid) - 1;
                memcpy(ssid, rest, sl);
                ssid[sl] = '\0';
                strncpy(pass, sp2 + 1, sizeof(pass) - 1);
                pass[sizeof(pass) - 1] = '\0';
            } else {
                strncpy(ssid, rest, sizeof(ssid) - 1);
                ssid[sizeof(ssid) - 1] = '\0';
            }
            if (wifi_connect_ssid(ssid, pass)) {
                char out[80];
                snprintf(out, sizeof(out), "Connected to Wi-Fi '%s'\n", ssid);
                term_out(out);
            } else {
                term_out("wifi connect: failed (passphrase required for WPA2)\n");
            }
        } else if (arg && strcmp(arg, "disconnect") == 0) {
            wifi_disconnect();
            term_out("Wi-Fi disconnected.\n");
        } else {
            char out[128];
            snprintf(out, sizeof(out), "Wi-Fi HW: %s | State: %s%s\n",
                     wifi_hw_label(),
                     wifi_is_connected() ? "Connected -> " : "Disconnected",
                     wifi_is_connected() ? wifi_connected_ssid() : "");
            term_out(out);
            term_out("Usage: wifi [scan | connect <ssid> [pass] | disconnect]\n");
        }
    } else if (strcmp(p, "run") == 0) {
        if (!arg || !*arg) term_out("usage: run <program.anv>\n");
        else cmd_run_anv(arg);
    } else if (strcmp(p, "reboot") == 0) {
        term_out("Rebooting...\n");
        acpi_reboot();
    } else if (strcmp(p, "shutdown") == 0) {
        term_out("Shutting down...\n");
        acpi_shutdown();
    } else if (strchr(p, '/') || (strlen(p) > 4 && strcmp(p + strlen(p) - 4, ".anv") == 0)) {
        cmd_run_anv(p);
    } else {
        char bin_path[128];
        snprintf(bin_path, sizeof(bin_path), "/bin/%s.anv", p);
        vfs_stat_t st;
        if (vfs_stat(bin_path, &st)) {
            cmd_run_anv(bin_path);
        } else {
            term_out("ash: command not found (try 'help')\n");
        }
    }
}

void ash_handle_char(char c) {
    if (c == '\r' || c == '\n') {
        term_out("\n");
        cmd_buf[cmd_len] = '\0';
        ash_exec_line(cmd_buf);
        cmd_len = 0;
        cmd_buf[0] = '\0';
        print_prompt();
    } else if (c == '\b' || c == 127) {
        if (cmd_len > 0) {
            cmd_len--;
            cmd_buf[cmd_len] = '\0';
            serial_puts("\b \b");
            gui_term_putchar('\b');
        }
    } else if (c >= 32 && c <= 126) {
        if (cmd_len < (int)sizeof(cmd_buf) - 1) {
            cmd_buf[cmd_len++] = c;
            cmd_buf[cmd_len] = '\0';
            serial_putc(c);
            gui_term_putchar(c);
        }
    }
}
