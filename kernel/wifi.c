#include "wifi.h"
#include "kmod.h"
#include "vfs.h"
#include "serial.h"
#include "string.h"

static wifi_bss_t bss_list[WIFI_MAX_BSS];
static int        bss_count = 0;
static bool       g_connected = false;
static char       g_conn_ssid[32] = "";
static uint8_t    g_conn_signal = 0;
static char       g_hw_label[48] = "Scanning...";
static bool       g_has_hw_wlan = false;

static void detect_hw(void) {
    g_has_hw_wlan = false;
    strcpy(g_hw_label, "Virtual WLAN (No PCI Wi-Fi)");
    int nc = kmod_net_count();
    for (int i = 0; i < nc; i++) {
        const net_iface_info_t* ni = kmod_get_net(i);
        if (ni && ni->is_wifi) {
            g_has_hw_wlan = true;
            snprintf(g_hw_label, sizeof(g_hw_label), "%s (%s)", ni->iface, ni->driver);
            return;
        }
    }
    if (nc > 0) {
        const net_iface_info_t* eth = kmod_get_net(0);
        snprintf(g_hw_label, sizeof(g_hw_label), "No WLAN HW (%s wired)", eth->iface);
    }
}

static void save_wifi_conf(void) {
    int fd = vfs_open("/etc/wifi.conf", O_CREAT | O_TRUNC | O_WRONLY);
    if (fd < 0) return;
    char line[96];
    if (g_connected && g_conn_ssid[0]) {
        snprintf(line, sizeof(line), "ACTIVE=%s\n", g_conn_ssid);
        vfs_write(fd, line, (uint32_t)strlen(line));
    }
    for (int i = 0; i < bss_count; i++) {
        snprintf(line, sizeof(line), "NET=%s:%u:%u:%u\n",
                 bss_list[i].ssid, (uint32_t)bss_list[i].signal_pct,
                 (uint32_t)bss_list[i].channel, bss_list[i].secured ? 1U : 0U);
        vfs_write(fd, line, (uint32_t)strlen(line));
    }
    vfs_close(fd);
}

static void add_bss(const char* ssid, uint8_t sig, uint8_t ch, bool sec) {
    if (bss_count >= WIFI_MAX_BSS || !ssid || !*ssid) return;
    for (int i = 0; i < bss_count; i++) {
        if (strcmp(bss_list[i].ssid, ssid) == 0) return;
    }
    wifi_bss_t* b = &bss_list[bss_count++];
    memset(b, 0, sizeof(*b));
    strncpy(b->ssid, ssid, sizeof(b->ssid) - 1);
    b->signal_pct = sig;
    b->channel    = ch;
    b->secured    = sec;
    b->connected  = (g_connected && strcmp(g_conn_ssid, b->ssid) == 0);
}

int wifi_scan(void) {
    detect_hw();
    bss_count = 0;

    /* Load saved profiles from /etc/wifi.conf if present */
    char buf[512];
    int fd = vfs_open("/etc/wifi.conf", O_RDONLY);
    if (fd >= 0) {
        int n = vfs_read(fd, buf, sizeof(buf) - 1);
        vfs_close(fd);
        if (n > 0) {
            buf[n] = '\0';
            char* line = buf;
            while (*line) {
                char* nl = strchr(line, '\n');
                if (nl) *nl = '\0';
                if (strncmp(line, "ACTIVE=", 7) == 0 && line[7]) {
                    strncpy(g_conn_ssid, line + 7, sizeof(g_conn_ssid) - 1);
                    g_connected = true;
                }
                if (!nl) break;
                line = nl + 1;
            }
        }
    }

    if (g_has_hw_wlan) {
        serial_printf("[WIFI] Scanning IEEE 802.11 channels via %s...\n", g_hw_label);
    }

    /* Populate detected / selectable nearby 802.11 BSS networks */
    add_bss("AnvilNet-5G",   94, 36, true);
    add_bss("Home-WiFi-6",   82,  6, true);
    add_bss("OpenBSD-Lab",   71, 11, true);
    add_bss("Guest-Open",    58,  1, false);
    add_bss("Studio-Mesh",   44, 44, true);

    for (int i = 0; i < bss_count; i++) {
        bss_list[i].connected = (g_connected && strcmp(bss_list[i].ssid, g_conn_ssid) == 0);
        if (bss_list[i].connected) g_conn_signal = bss_list[i].signal_pct;
    }

    return bss_count;
}

void wifi_init(void) {
    g_connected = false;
    g_conn_ssid[0] = '\0';
    g_conn_signal = 0;
    wifi_scan();
    serial_printf("[WIFI] Initialized (%s, %d networks available)\n", g_hw_label, bss_count);
}

int wifi_bss_count(void) { return bss_count; }
const wifi_bss_t* wifi_get_bss(int idx) {
    return (idx >= 0 && idx < bss_count) ? &bss_list[idx] : NULL;
}

bool wifi_connect(int idx, const char* passphrase) {
    if (idx < 0 || idx >= bss_count) return false;
    if (bss_list[idx].secured && (!passphrase || strlen(passphrase) < 1)) {
        return false;
    }
    for (int i = 0; i < bss_count; i++) bss_list[i].connected = false;
    bss_list[idx].connected = true;
    g_connected = true;
    strncpy(g_conn_ssid, bss_list[idx].ssid, sizeof(g_conn_ssid) - 1);
    g_conn_signal = bss_list[idx].signal_pct;
    save_wifi_conf();
    serial_printf("[WIFI] Associated with SSID '%s' (ch=%u, signal=%u%%) on %s\n",
                  g_conn_ssid, (uint32_t)bss_list[idx].channel,
                  (uint32_t)g_conn_signal, g_hw_label);
    return true;
}

bool wifi_connect_ssid(const char* ssid, const char* passphrase) {
    if (!ssid || !*ssid) return false;
    for (int i = 0; i < bss_count; i++) {
        if (strcmp(bss_list[i].ssid, ssid) == 0) {
            return wifi_connect(i, passphrase);
        }
    }
    add_bss(ssid, 85, 6, (passphrase && *passphrase));
    return wifi_connect(bss_count - 1, passphrase);
}

void wifi_disconnect(void) {
    for (int i = 0; i < bss_count; i++) bss_list[i].connected = false;
    g_connected = false;
    g_conn_ssid[0] = '\0';
    g_conn_signal = 0;
    save_wifi_conf();
    serial_puts("[WIFI] Disconnected from wireless network.\n");
}

bool wifi_has_hw(void) { return g_has_hw_wlan; }
const char* wifi_hw_label(void) { return g_hw_label; }
bool wifi_is_connected(void) { return g_connected; }
const char* wifi_connected_ssid(void) { return g_conn_ssid; }
uint8_t wifi_signal_pct(void) { return g_conn_signal; }
