#pragma once

// Required by pico_cyw43_arch_lwip_threadsafe_background.
// These settings are tuned for a single-connection HTTP client with minimal RAM use.

#define NO_SYS                          1
#define LWIP_SOCKET                     0
#define LWIP_NETCONN                    0

#define LWIP_ARP                        1
#define LWIP_ETHERNET                   1
#define LWIP_ICMP                       1
#define LWIP_DHCP                       1
#define LWIP_DNS                        1
#define LWIP_TCP                        1
#define LWIP_UDP                        1

#define TCP_MSS                         1460
#define TCP_WND                         (4 * TCP_MSS)
#define TCP_SND_BUF                     (4 * TCP_MSS)
#define TCP_SND_QUEUELEN                ((2 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))

#define MEM_ALIGNMENT                   4
#define MEM_SIZE                        24000  // increased for AP + HTTP server + DHCP
#define PBUF_POOL_SIZE                  24
#define MEMP_NUM_TCP_SEG                24
#define MEMP_NUM_ARP_QUEUE              5
#define MEMP_NUM_UDP_PCB                4      // DHCP server needs 1 UDP PCB
#define MEMP_NUM_TCP_PCB_LISTEN         4

#define LWIP_NETIF_STATUS_CALLBACK      1
#define LWIP_NETIF_LINK_CALLBACK        1
#define LWIP_NETIF_HOSTNAME             1
#define LWIP_TCP_KEEPALIVE              1
#define LWIP_STATS                      0
#define LWIP_RAND()                     ((u32_t)rand())
