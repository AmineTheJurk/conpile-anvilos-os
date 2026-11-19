#include "kmod.h"
#include "wifi.h"
#include "vfs.h"
#include "serial.h"
#include "string.h"

static wifi_bss_t bss_list[WIFI_MAX_BSS];
static int bss_count;
static bool g_connected;
static char g_conn_ssid[32];
static uint8_t g_conn_signal;
static char g_hw_label[48] = "No Wi-Fi hardware";
static bool g_has_hw_wlan;

static void detect_hw(void) {
    g_has_hw_wlan = false;
    strcpy(g_hw_label, "No Wi-Fi hardware");
    int count = kmod_net_count();
    for (int i = 0; i < count; i++) {
        const net_iface_info_t* ni = kmod_get_net(i);
        if (ni && ni->is_wifi) {
            g_has_hw_wlan = true;
            snprintf(g_hw_label, sizeof(g_hw_label), "%s (%s)", ni->iface, ni->driver);
            return;
        }
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
    vfs_close(fd);
}

static void load_wifi_conf(void) {
    char buf[512];
    int fd = vfs_open("/etc/wifi.conf", O_RDONLY);
    if (fd < 0) return;
    int n = vfs_read(fd, buf, sizeof(buf) - 1);
    vfs_close(fd);
    if (n <= 0) return;
    buf[n] = '\0';
    char* line = buf;
    while (*line) {
        char* nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        if (strncmp(line, "ACTIVE=", 7) == 0 && line[7]) {
            strncpy(g_conn_ssid, line + 7, sizeof(g_conn_ssid) - 1);
        }
        if (!nl) break;
        line = nl + 1;
    }
}

int wifi_scan(void) {
    detect_hw();
    bss_count = 0;
    memset(bss_list, 0, sizeof(bss_list));
    if (!g_has_hw_wlan) {
        serial_printf("[WIFI] Scan unavailable: %s\n", g_hw_label);
        return 0;
    }

    int count = kmod_net_count();
    for (int i = 0; i < count; i++) {
        const net_iface_info_t* ni = kmod_get_net(i);
        if (!ni || !ni->is_wifi) continue;
        if (!ni->wifi_scan) {
            serial_printf("[WIFI] %s has no radio scan implementation yet.\n", ni->driver);
            continue;
        }
        kmod_wifi_bss_t found[WIFI_MAX_BSS];
        memset(found, 0, sizeof(found));
        int found_count = ni->wifi_scan(found, WIFI_MAX_BSS);
        if (found_count < 0) found_count = 0;
        if (found_count > WIFI_MAX_BSS) found_count = WIFI_MAX_BSS;
        for (int j = 0; j < found_count; j++) {
            wifi_bss_t* b = &bss_list[bss_count++];
            memset(b, 0, sizeof(*b));
            strncpy(b->ssid, found[j].ssid, sizeof(b->ssid) - 1);
            b->signal_pct = found[j].signal_pct;
            b->channel = found[j].channel;
            b->secured = found[j].secured;
            b->connected = g_connected && strcmp(g_conn_ssid, b->ssid) == 0;
            if (bss_count >= WIFI_MAX_BSS) break;
        }
        break;
    }
    return bss_count;
}

void wifi_init(void) {
    g_connected = false;
    g_conn_ssid[0] = '\0';
    g_conn_signal = 0;
    load_wifi_conf();
    (void)wifi_scan();
    if (g_conn_ssid[0]) g_connected = false;
    serial_printf("[WIFI] Initialized (%s, %d real scan results)\n", g_hw_label, bss_count);
}

int wifi_bss_count(void) { return bss_count; }
const wifi_bss_t* wifi_get_bss(int idx) {
    return (idx >= 0 && idx < bss_count) ? &bss_list[idx] : NULL;
}

bool wifi_connect(int idx, const char* passphrase) {
    if (idx < 0 || idx >= bss_count) return false;
    wifi_bss_t* b = &bss_list[idx];
    int count = kmod_net_count();
    for (int i = 0; i < count; i++) {
        const net_iface_info_t* ni = kmod_get_net(i);
        if (!ni || !ni->is_wifi || !ni->wifi_connect) continue;
        if (!ni->wifi_connect(b->ssid, passphrase)) continue;
        for (int j = 0; j < bss_count; j++) bss_list[j].connected = false;
        b->connected = true;
        g_connected = true;
        strncpy(g_conn_ssid, b->ssid, sizeof(g_conn_ssid) - 1);
        g_conn_signal = b->signal_pct;
        save_wifi_conf();
        serial_printf("[WIFI] Associated with SSID '%s' on %s\n", b->ssid, g_hw_label);
        return true;
    }
    serial_puts("[WIFI] Association unavailable: no WLAN module control implementation.\n");
    return false;
}

bool wifi_connect_ssid(const char* ssid, const char* passphrase) {
    if (!ssid || !*ssid) return false;
    wifi_scan();
    for (int i = 0; i < bss_count; i++) {
        if (strcmp(bss_list[i].ssid, ssid) == 0) return wifi_connect(i, passphrase);
    }
    int count = kmod_net_count();
    for (int i = 0; i < count; i++) {
        const net_iface_info_t* ni = kmod_get_net(i);
        if (ni && ni->is_wifi && ni->wifi_connect)
            return wifi_connect_ssid(ssid, passphrase);
    }
    return false;
}

void wifi_disconnect(void) {
    int count = kmod_net_count();
    for (int i = 0; i < count; i++) {
        const net_iface_info_t* ni = kmod_get_net(i);
        if (ni && ni->is_wifi && ni->wifi_disconnect) ni->wifi_disconnect();
    }
    for (int i = 0; i < bss_count; i++) bss_list[i].connected = false;
    g_connected = false;
    g_conn_ssid[0] = '\0';
    g_conn_signal = 0;
    save_wifi_conf();
}

bool wifi_has_hw(void) { return g_has_hw_wlan; }
const char* wifi_hw_label(void) { return g_hw_label; }
bool wifi_is_connected(void) { return g_connected; }
const char* wifi_connected_ssid(void) { return g_conn_ssid; }
uint8_t wifi_signal_pct(void) { return g_conn_signal; }
