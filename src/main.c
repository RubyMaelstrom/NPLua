// main.c
#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "nplua_config.h"
#include "telnet.h"
#include "console.h"
#include "core.h"

#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "lwip/ip6_addr.h"
#include "lwip/inet.h"

static int lastLinkStatus = CYW43_LINK_DOWN;
static absolute_time_t nextWifiHealthCheck;

static int wifiInit(void) {
    if (cyw43_arch_init_with_country(NPLUA_WIFI_COUNTRY)) {
        printf("cyw43_arch_init_with_country() failed\r\n");
        return -1;
    }

    cyw43_arch_enable_sta_mode();

    // The CYW43/lwIP glue creates the link-local address. Enable SLAAC so
    // router advertisements can also supply a globally routable address.
    cyw43_arch_lwip_begin();
    if (netif_default) {
        netif_default->ip6_autoconfig_enabled = 1;
    }
    cyw43_arch_lwip_end();

    return 0;
}

static void printNetworkAddresses(void) {
    cyw43_arch_lwip_begin();
    const struct netif *n = netif_default;
    if (n) {
        const ip4_addr_t *ip = netif_ip4_addr(n);
        printf("IPv4 address: %s\r\n", ip4addr_ntoa(ip));

        for (int i = 0; i < LWIP_IPV6_NUM_ADDRESSES; ++i) {
            const ip6_addr_t *ip6 = netif_ip6_addr(n, i);
            if (!ip6_addr_isany(ip6)) {
                printf("IPv6 address %d: %s (state 0x%02x)\r\n",
                       i,
                       ip6addr_ntoa(ip6),
                       netif_ip6_addr_state(n, i));
            }
        }
    }
    cyw43_arch_lwip_end();
}

static int wifiConnect(void) {
    printf("Connecting to WiFi SSID '%s'...\r\n", NPLUA_WIFI_SSID);

    int err = cyw43_arch_wifi_connect_timeout_ms(
        NPLUA_WIFI_SSID,
        NPLUA_WIFI_PASSWORD,
        NPLUA_WIFI_AUTH,
        NPLUA_WIFI_CONNECT_TIMEOUT_MS
    );
    if (err) {
        printf("WiFi connect failed, error=%d\r\n", err);
        return -1;
    }

    printf("WiFi connected.\r\n");
    lastLinkStatus = CYW43_LINK_UP;
    printNetworkAddresses();

    return 0;
}

static void wifiMaintain(void) {
    if (!time_reached(nextWifiHealthCheck)) return;

    nextWifiHealthCheck = make_timeout_time_ms(NPLUA_WIFI_HEALTH_INTERVAL_MS);
    int status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);

    if (status == CYW43_LINK_UP) {
        lastLinkStatus = status;
        return;
    }

    if (lastLinkStatus == CYW43_LINK_UP) {
        printf("WiFi link lost (status=%d).\r\n", status);
        telnetCloseActive();
        telnetProcess();
    }
    lastLinkStatus = status;

    // JOIN and NOIP are transitional states; give association/DHCP time.
    if (status == CYW43_LINK_JOIN || status == CYW43_LINK_NOIP) return;

    if (wifiConnect() != 0) {
        nextWifiHealthCheck = make_timeout_time_ms(NPLUA_WIFI_RETRY_DELAY_MS);
    }
}

int main(void) {
    stdio_init_all();
    sleep_ms(2000);

    printf("\r\nNPLua starting up (WiFi + telnet + multicore Lua)...\r\n");

    if (wifiInit() != 0) {
        printf("WiFi initialization failed, halting.\r\n");
        for (;;) tight_loop_contents();
    }

    while (wifiConnect() != 0) {
        sleep_ms(NPLUA_WIFI_RETRY_DELAY_MS);
    }
    nextWifiHealthCheck = make_timeout_time_ms(NPLUA_WIFI_HEALTH_INTERVAL_MS);

    // Telnet and console callbacks run in the CYW43 background context.
    telnetInit();

    // Launch core1 to run Lua jobs
    npluaInitLuaCore();

    for (;;) {
        wifiMaintain();

        if (npluaTakeLuaCompletion()) {
            const char *prompt = consoleGetPrompt();
            if (prompt) {
                telnetSend(prompt);
            }
        }

        // Apply deferred connection closes outside lwIP callbacks.
        telnetProcess();

        sleep_ms(10);
    }
}
