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
    /* Do not persist passphrases; only record the last successfully associated SSID. */
    if (g_connected && g_conn_ssid[0]) {
        char line[48];
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
            g_conn_ssid[sizeof(g_conn_ssid) - 1] = '\0';
        }
        if (!nl) break;
        line = nl + 1;
    }
}

static void update_connected_state(void) {
    for (int i = 0; i < bss_count; i++) {
        bss_list[i].connected = g_connected && strcmp(g_conn_ssid, bss_list[i].ssid) == 0;
        if (bss_list[i].connected) g_conn_signal = bss_list[i].signal_pct;
    }
}

static void add_scan_result(const kmod_wifi_bss_t* found) {
    if (!found || !found->ssid[0]) return;
    /* Merge duplicate SSIDs reported by multiple radios, retaining the strongest. */
    for (int i = 0; i < bss_count; i++) {
        if (strcmp(bss_list[i].ssid, found->ssid) == 0) {
            if (found->signal_pct > bss_list[i].signal_pct) {
                bss_list[i].signal_pct = found->signal_pct;
                bss_list[i].channel = found->channel;
            }
            bss_list[i].secured = bss_list[i].secured || found->secured;
            return;
        }
    }
    if (bss_count >= WIFI_MAX_BSS) return;
    wifi_bss_t* b = &bss_list[bss_count++];
    memset(b, 0, sizeof(*b));
    strncpy(b->ssid, found->ssid, sizeof(b->ssid) - 1);
    b->ssid[sizeof(b->ssid) - 1] = '\0';
    b->signal_pct = found->signal_pct > 100 ? 100 : found->signal_pct;
    b->channel = found->channel;
    b->secured = found->secured;
}

int wifi_scan(void) {
    detect_hw();
    bss_count = 0;
    memset(bss_list, 0, sizeof(bss_list));
    if (!g_has_hw_wlan) {
        serial_printf("[WIFI] Scan unavailable: %s\n", g_hw_label);
        return 0;
    }

    int radios = kmod_net_count();
    bool scan_supported = false;
    for (int i = 0; i < radios && bss_count < WIFI_MAX_BSS; i++) {
        const net_iface_info_t* ni = kmod_get_net(i);
        if (!ni || !ni->is_wifi || !ni->wifi_scan) continue;
        scan_supported = true;
        kmod_wifi_bss_t found[WIFI_MAX_BSS];
        memset(found, 0, sizeof(found));
        int n = ni->wifi_scan(found, WIFI_MAX_BSS);
        if (n < 0) continue;
        if (n > WIFI_MAX_BSS) n = WIFI_MAX_BSS;
        for (int j = 0; j < n && bss_count < WIFI_MAX_BSS; j++) add_scan_result(&found[j]);
    }
    if (!scan_supported)
        serial_printf("[WIFI] %s detected, but its module has no scan callback.\n", g_hw_label);
    update_connected_state();
    return bss_count;
}

void wifi_init(void) {
    g_connected = false;
    g_conn_ssid[0] = '\0';
    g_conn_signal = 0;
    load_wifi_conf();
    /* Saved SSID is a profile hint only; no connection is claimed without association. */
    char saved_ssid[sizeof(g_conn_ssid)];
    strncpy(saved_ssid, g_conn_ssid, sizeof(saved_ssid) - 1);
    saved_ssid[sizeof(saved_ssid) - 1] = '\0';
    g_conn_ssid[0] = '\0';
    (void)wifi_scan();
    strncpy(g_conn_ssid, saved_ssid, sizeof(g_conn_ssid) - 1);
    g_conn_ssid[sizeof(g_conn_ssid) - 1] = '\0';
    g_connected = false;
    g_conn_signal = 0;
    serial_printf("[WIFI] Initialized (%s, %d scan result(s); disconnected)\n", g_hw_label, bss_count);
}

int wifi_bss_count(void) { return bss_count; }
const wifi_bss_t* wifi_get_bss(int idx) {
    return (idx >= 0 && idx < bss_count) ? &bss_list[idx] : NULL;
}

static bool connect_radio(const char* ssid, const char* passphrase) {
    int count = kmod_net_count();
    for (int i = 0; i < count; i++) {
        const net_iface_info_t* ni = kmod_get_net(i);
        if (!ni || !ni->is_wifi || !ni->wifi_connect) continue;
        if (ni->wifi_connect(ssid, passphrase)) {
            serial_printf("[WIFI] Radio %s accepted association with '%s'.\n", ni->iface, ssid);
            return true;
        }
    }
    return false;
}

static void mark_connected(const char* ssid) {
    g_connected = true;
    strncpy(g_conn_ssid, ssid, sizeof(g_conn_ssid) - 1);
    g_conn_ssid[sizeof(g_conn_ssid) - 1] = '\0';
    g_conn_signal = 0;
    for (int i = 0; i < bss_count; i++) {
        bss_list[i].connected = strcmp(bss_list[i].ssid, ssid) == 0;
        if (bss_list[i].connected) g_conn_signal = bss_list[i].signal_pct;
    }
    save_wifi_conf();
}

bool wifi_connect(int idx, const char* passphrase) {
    if (idx < 0 || idx >= bss_count) return false;
    const wifi_bss_t* b = &bss_list[idx];
    if (b->secured && (!passphrase || !*passphrase)) return false;
    if (!connect_radio(b->ssid, passphrase)) {
        serial_puts("[WIFI] Association failed or unsupported by this radio driver.\n");
        return false;
    }
    mark_connected(b->ssid);
    return true;
}

bool wifi_connect_ssid(const char* ssid, const char* passphrase) {
    if (!ssid || !*ssid || strlen(ssid) >= sizeof(g_conn_ssid)) return false;
    /* Scan first so known security requirements are checked. Hidden SSIDs go directly
       to the radio callback instead of recursively rescanning forever. */
    (void)wifi_scan();
    for (int i = 0; i < bss_count; i++) {
        if (strcmp(bss_list[i].ssid, ssid) == 0) return wifi_connect(i, passphrase);
    }
    if (!connect_radio(ssid, passphrase)) {
        serial_puts("[WIFI] Hidden-network association failed or is unsupported.\n");
        return false;
    }
    mark_connected(ssid);
    return true;
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
