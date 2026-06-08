#pragma once

// Minimal DHCP server for AP mode.
// Assigns IPs in the 192.168.4.x/24 subnet to connecting clients.

#include "lwip/ip4_addr.h"

struct udp_pcb;

typedef struct {
    ip4_addr_t      ip;           // server / gateway IP (192.168.4.1)
    ip4_addr_t      nm;           // netmask (255.255.255.0)
    struct udp_pcb *pcb;
    uint8_t         next_client;  // last octet of next IP to hand out (starts at 2)
} dhcp_server_t;

void dhcp_server_init(dhcp_server_t *d, ip4_addr_t *ip, ip4_addr_t *nm);
void dhcp_server_deinit(dhcp_server_t *d);
