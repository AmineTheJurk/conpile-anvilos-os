#include "wifi.h"
#include "kmod.h"
#include "netstack.h"
#include "vfs.h"
#include "serial.h"
#include "string.h"

static wifi_bss_t bss_list[WIFI_MAX_BSS];
static int bss_count;
static bool g_connected;
static bool g_has_hw_wlan;
static char g_conn_ssid[32];
static char g_hw_label[48] = "No Wi-Fi hardware";
static char g_conn_name[32];
static wifi_connection_type_t g_connection_type;

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
    if (g_connection_type == WIFI_CONNECTION_WIRELESS && g_connected && g_conn_ssid[0]) {
        char line[48];
        snprintf(line, sizeof(line), "ACTIVE=%s\n", g_conn_ssid);
        vfs_write(fd, line, (uint32_t)strlen(line));
    } else if (g_connection_type == WIFI_CONNECTION_ETHERNET && g_connected) {
        const char line[] = "TRANSPORT=ETHERNET\n";
        vfs_write(fd, line, sizeof(line) - 1);
    }
    vfs_close(fd);
}

int wifi_scan(void) {
    detect_hw();
    bss_count = 0;
    memset(bss_list, 0, sizeof(bss_list));
    /* The loaded wireless modules currently provide no scan callback. Never invent BSSs. */
    if (g_has_hw_wlan)
        serial_printf("[WIFI] %s detected; this radio driver does not expose scanning yet.\n", g_hw_label);
    else
        serial_printf("[WIFI] Scan unavailable: %s\n", g_hw_label);
    return 0;
}

void wifi_init(void) {
    g_connected = false;
    g_connection_type = WIFI_CONNECTION_NONE;
    g_conn_ssid[0] = '\0';
    g_conn_name[0] = '\0';
    (void)wifi_scan();
    if (wifi_ethernet_link_up())
        serial_puts("[NET] Ethernet carrier detected; select wired networking with wifi_use_ethernet().\n");
}

int wifi_bss_count(void) { return bss_count; }
const wifi_bss_t* wifi_get_bss(int idx) {
    return (idx >= 0 && idx < bss_count) ? &bss_list[idx] : NULL;
}

bool wifi_connect(int idx, const char* passphrase) {
    (void)idx;
    (void)passphrase;
    serial_puts("[WIFI] Cannot associate: no wireless module currently implements 802.11 control.\n");
    return false;
}

bool wifi_connect_ssid(const char* ssid, const char* passphrase) {
    (void)ssid;
    (void)passphrase;
    serial_puts("[WIFI] Cannot associate: no wireless module currently implements 802.11 control.\n");
    return false;
}

bool wifi_ethernet_link_up(void) {
    int count = netstack_interface_count();
    for (int i = 0; i < count; i++) {
        const net_interface_t* iface = netstack_get_interface(i);
        if (iface && !iface->is_wifi && iface->link_up) return true;
    }
    return false;
}

bool wifi_use_ethernet(void) {
    int count = netstack_interface_count();
    for (int i = 0; i < count; i++) {
        const net_interface_t* iface = netstack_get_interface(i);
        if (!iface || iface->is_wifi || !iface->link_up) continue;
        g_connection_type = WIFI_CONNECTION_ETHERNET;
        g_connected = true; /* Ethernet carrier is up; this does not imply DHCP/IP reachability. */
        g_conn_ssid[0] = '\0';
        strncpy(g_conn_name, iface->name, sizeof(g_conn_name) - 1);
        g_conn_name[sizeof(g_conn_name) - 1] = '\0';
        for (int j = 0; j < bss_count; j++) bss_list[j].connected = false;
        save_wifi_conf();
        serial_printf("[NET] Selected wired interface %s (link up; IP configuration is separate).\n",
                      g_conn_name);
        return true;
    }
    serial_puts("[NET] No wired Ethernet interface currently has link.\n");
    return false;
}

void wifi_disconnect(void) {
    for (int i = 0; i < bss_count; i++) bss_list[i].connected = false;
    g_connected = false;
    g_connection_type = WIFI_CONNECTION_NONE;
    g_conn_ssid[0] = '\0';
    g_conn_name[0] = '\0';
    save_wifi_conf();
}

bool wifi_has_hw(void) { return g_has_hw_wlan; }
const char* wifi_hw_label(void) { return g_hw_label; }
bool wifi_is_connected(void) { return g_connected; }
const char* wifi_connected_ssid(void) { return g_conn_ssid; }
uint8_t wifi_signal_pct(void) { return 0; }
wifi_connection_type_t wifi_connection_type(void) { return g_connection_type; }
const char* wifi_connection_name(void) { return g_conn_name; }
