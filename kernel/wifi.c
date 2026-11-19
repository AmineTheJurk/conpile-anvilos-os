#include "wifi.h"
#include "kmod.h"
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
    if (count > 0) {
        const net_iface_info_t* wired = kmod_get_net(0);
        if (wired) snprintf(g_hw_label, sizeof(g_hw_label), "No WLAN HW (%s wired)", wired->iface);
    }
}

static void save_wifi_conf(void) {
    int fd = vfs_open("/etc/wifi.conf", O_CREAT | O_TRUNC | O_WRONLY);
    if (fd < 0) return;
    if (g_connected && g_conn_ssid[0]) {
        char line[48];
        snprintf(line, sizeof(line), "ACTIVE=%s\n", g_conn_ssid);
        vfs_write(fd, line, (uint32_t)strlen(line));
    }
    vfs_close(fd);
}

int wifi_scan(void) {
    detect_hw();
    bss_count = 0;
    memset(bss_list, 0, sizeof(bss_list));
    if (!g_has_hw_wlan) {
        serial_printf("[WIFI] Scan unavailable: %s\n", g_hw_label);
        return 0;
    }

    /* No current AnvilOS WLAN module exposes scan results yet. Do not invent BSSs. */
    serial_printf("[WIFI] %s detected, but this driver has no scan/association API yet.\n",
                  g_hw_label);
    return 0;
}

void wifi_init(void) {
    g_connected = false;
    g_conn_ssid[0] = '\0';
    g_conn_signal = 0;
    (void)wifi_scan();
    serial_printf("[WIFI] Initialized (%s, %d real scan results)\n", g_hw_label, bss_count);
}

int wifi_bss_count(void) { return bss_count; }
const wifi_bss_t* wifi_get_bss(int idx) {
    return (idx >= 0 && idx < bss_count) ? &bss_list[idx] : NULL;
}

bool wifi_connect(int idx, const char* passphrase) {
    (void)passphrase;
    if (idx < 0 || idx >= bss_count || !g_has_hw_wlan) return false;
    serial_puts("[WIFI] Association is unavailable: no WLAN driver control API is implemented.\n");
    return false;
}

bool wifi_connect_ssid(const char* ssid, const char* passphrase) {
    (void)ssid;
    (void)passphrase;
    serial_puts("[WIFI] Cannot connect: no real scan/association support is available.\n");
    return false;
}

void wifi_disconnect(void) {
    if (!g_connected) return;
    g_connected = false;
    g_conn_ssid[0] = '\0';
    g_conn_signal = 0;
    save_wifi_conf();
    serial_puts("[WIFI] Disconnected.\n");
}

bool wifi_has_hw(void) { return g_has_hw_wlan; }
const char* wifi_hw_label(void) { return g_hw_label; }
bool wifi_is_connected(void) { return g_connected; }
const char* wifi_connected_ssid(void) { return g_conn_ssid; }
uint8_t wifi_signal_pct(void) { return g_conn_signal; }
