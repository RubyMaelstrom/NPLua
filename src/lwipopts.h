#ifndef __LWIPOPTS_H__
#define __LWIPOPTS_H__

// Pico W/2W + cyw43_arch_lwip_threadsafe_background
#define NO_SYS                          1
#define SYS_LIGHTWEIGHT_PROT            1

// raw API only (we don't use sockets/netconn)
#define LWIP_SOCKET                     0
#define LWIP_NETCONN                    0
#define LWIP_NETIF_API                  0
#define LWIP_RAW                        1

// Make sure we don't clash with newlib's struct timeval
#define LWIP_TIMEVAL_PRIVATE            0

// Protocols
#define LWIP_TCP                        1
#define LWIP_UDP                        1
#define LWIP_ICMP                       1
#define LWIP_DHCP                       1
#define LWIP_DNS                        1

// IP versions
#define LWIP_IPV4                       1
#define LWIP_IPV6                       0

// Memory & buffers
#define MEM_ALIGNMENT                   4
#define MEM_SIZE                        6000

#define MEMP_NUM_PBUF                   16
#define MEMP_NUM_TCP_PCB                8
#define MEMP_NUM_UDP_PCB                4
#define MEMP_NUM_TCP_SEG                32

#define MEMP_NUM_SYS_TIMEOUT            16

// TCP tuning
#define TCP_MSS                         1460
#define TCP_SND_BUF                     (2 * TCP_MSS)
#define TCP_WND                         (2 * TCP_MSS)

// Callbacks used by cyw43 arch
#define LWIP_NETIF_STATUS_CALLBACK      1
#define LWIP_NETIF_LINK_CALLBACK        1

// Odds & ends
#define LWIP_CHECKSUM_ON_COPY           1
#define LWIP_STATS                      0
#define LWIP_STATS_DISPLAY              0

#endif /* __LWIPOPTS_H__ */