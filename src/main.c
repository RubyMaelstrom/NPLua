// main.c
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/multicore.h"

#include "luaEmbed.h"
#include "telnet.h"
#include "console.h"
#include "core.h"

#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "lwip/inet.h"

#ifndef WIFI_SSID
#define WIFI_SSID     "YourWifiSSID"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "YourWifiPassword"
#endif

static int wifiConnect(void) {
    if (cyw43_arch_init_with_country(CYW43_COUNTRY_GERMANY)) {
        printf("cyw43_arch_init_with_country() failed\r\n");
        return -1;
    }

    cyw43_arch_enable_sta_mode();
    printf("Connecting to WiFi SSID '%s'...\r\n", WIFI_SSID);

    int err = cyw43_arch_wifi_connect_timeout_ms(
        WIFI_SSID,
        WIFI_PASSWORD,
        CYW43_AUTH_WPA2_MIXED_PSK,
        30000
    );
    if (err) {
        printf("WiFi connect failed, error=%d\r\n", err);
        return -1;
    }

    printf("WiFi connected.\r\n");

    struct netif *n = netif_default;
    if (n) {
        const ip4_addr_t *ip = netif_ip4_addr(n);
        printf("IP address: %s\r\n", ip4addr_ntoa(ip));
    }

    return 0;
}

static void consoleWriteAdapter(const char *text) {
    if (text) {
        telnetSend(text);
    }
}

static void consolePrintfAdapter(const char *fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    telnetSend(buf);
}

int main(void) {
    stdio_init_all();
    sleep_ms(2000);

    printf("\r\nNPLua starting up (WiFi + telnet + multicore Lua)...\r\n");

    if (wifiConnect() != 0) {
        printf("WiFi setup failed, halting.\r\n");
        while (1) {
            sleep_ms(1000);
        }
    }

    // telnet/console live on core0
    telnetInit();
    consoleInit(consoleWriteAdapter, consolePrintfAdapter);

    // Launch core1 to run Lua jobs
    npluaInitLuaCore();

    for (;;) {
        // Network / WiFi polling
        cyw43_arch_poll();

        // Drain any queued output from core1 (Lua) and send to telnet
        npluaDrainOutput();

        sleep_ms(10);
    }

    cyw43_arch_deinit();
    return 0;
}