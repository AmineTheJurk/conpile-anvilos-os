#ifndef ANVIL_WIFI_H
#define ANVIL_WIFI_H

#include "types.h"

#define WIFI_MAX_BSS 8

typedef struct {
    char    ssid[32];
    uint8_t signal_pct; /* 0..100 */
    uint8_t channel;
    bool    secured;
    bool    connected;
} wifi_bss_t;

void              wifi_init(void);
int               wifi_scan(void);
int               wifi_bss_count(void);
const wifi_bss_t* wifi_get_bss(int idx);
bool              wifi_connect(int idx, const char* passphrase);
bool              wifi_connect_ssid(const char* ssid, const char* passphrase);
void              wifi_disconnect(void);
bool              wifi_has_hw(void);
const char*       wifi_hw_label(void);
bool              wifi_is_connected(void);
const char*       wifi_connected_ssid(void);
uint8_t           wifi_signal_pct(void);

#endif
