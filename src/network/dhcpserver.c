#include "dhcpserver.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include <string.h>

// ---------------------------------------------------------------------------
// Minimal DHCP server — handles DISCOVER → OFFER and REQUEST → ACK.
// Based on the pattern from pico-examples/pico_w/wifi/access_point.
// ---------------------------------------------------------------------------

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68

#define DHCP_OP_REQUEST 1
#define DHCP_OP_REPLY   2

#define DHCPDISCOVER 1
#define DHCPOFFER    2
#define DHCPREQUEST  3
#define DHCPACK      5

#define OPT_SUBNET_MASK  1
#define OPT_ROUTER       3
#define OPT_DNS          6
#define OPT_LEASE_TIME  51
#define OPT_MSG_TYPE    53
#define OPT_SERVER_ID   54
#define OPT_END        255

// Canonical DHCP packet layout (fixed + magic cookie, options follow).
typedef struct __attribute__((packed)) {
    uint8_t  op, htype, hlen, hops;
    uint32_t xid;
    uint16_t secs, flags;
    uint8_t  ciaddr[4];
    uint8_t  yiaddr[4];   // "your" IP — filled in by server
    uint8_t  siaddr[4];   // server IP
    uint8_t  giaddr[4];   // relay agent (unused)
    uint8_t  chaddr[16];  // client hardware address
    uint8_t  sname[64];
    uint8_t  file[128];
    uint8_t  magic[4];    // 99.130.83.99
    uint8_t  options[308];
} dhcp_msg_t;

static const uint8_t MAGIC[4] = {99, 130, 83, 99};

// ---- build and send a OFFER or ACK ----------------------------------------

static void send_reply(dhcp_server_t *d, struct udp_pcb *pcb,
                       dhcp_msg_t *req, uint8_t msg_type, const uint8_t client_ip[4])
{
    dhcp_msg_t r;
    memset(&r, 0, sizeof(r));

    r.op    = DHCP_OP_REPLY;
    r.htype = req->htype;
    r.hlen  = req->hlen;
    r.xid   = req->xid;
    r.flags = req->flags;
    memcpy(r.chaddr, req->chaddr, 16);
    memcpy(r.magic,  MAGIC, 4);
    memcpy(r.yiaddr, client_ip, 4);

    // siaddr = server IP
    r.siaddr[0] = ip4_addr1(&d->ip);
    r.siaddr[1] = ip4_addr2(&d->ip);
    r.siaddr[2] = ip4_addr3(&d->ip);
    r.siaddr[3] = ip4_addr4(&d->ip);

    // Options
    uint8_t *o = r.options;

    *o++ = OPT_MSG_TYPE;   *o++ = 1; *o++ = msg_type;

    *o++ = OPT_SERVER_ID;  *o++ = 4;
    *o++ = r.siaddr[0]; *o++ = r.siaddr[1]; *o++ = r.siaddr[2]; *o++ = r.siaddr[3];

    // Lease time: 24 hours = 0x00015180
    *o++ = OPT_LEASE_TIME; *o++ = 4;
    *o++ = 0x00; *o++ = 0x01; *o++ = 0x51; *o++ = 0x80;

    *o++ = OPT_SUBNET_MASK; *o++ = 4;
    *o++ = ip4_addr1(&d->nm); *o++ = ip4_addr2(&d->nm);
    *o++ = ip4_addr3(&d->nm); *o++ = ip4_addr4(&d->nm);

    *o++ = OPT_ROUTER;     *o++ = 4;
    *o++ = r.siaddr[0]; *o++ = r.siaddr[1]; *o++ = r.siaddr[2]; *o++ = r.siaddr[3];

    *o++ = OPT_DNS;        *o++ = 4;
    *o++ = r.siaddr[0]; *o++ = r.siaddr[1]; *o++ = r.siaddr[2]; *o++ = r.siaddr[3];

    *o++ = OPT_END;

    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, sizeof(r), PBUF_RAM);
    if (!p) return;
    memcpy(p->payload, &r, sizeof(r));

    ip4_addr_t broadcast;
    IP4_ADDR(&broadcast, 255, 255, 255, 255);
    ip_addr_t dst; ip_addr_copy_from_ip4(dst, broadcast);
    udp_sendto(pcb, p, &dst, DHCP_CLIENT_PORT);
    pbuf_free(p);
}

// ---- incoming packet callback ---------------------------------------------

static void on_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                    const ip_addr_t *addr, u16_t port)
{
    dhcp_server_t *d = (dhcp_server_t *)arg;
    (void)addr; (void)port;

    if (p->tot_len < 240) { pbuf_free(p); return; }

    dhcp_msg_t req;
    pbuf_copy_partial(p, &req, sizeof(req), 0);
    pbuf_free(p);

    if (req.op != DHCP_OP_REQUEST) return;
    if (memcmp(req.magic, MAGIC, 4) != 0) return;

    // Parse message type from options
    uint8_t msg_type = 0;
    uint8_t *opt = req.options;
    uint8_t *end = opt + sizeof(req.options) - 2;
    while (opt < end && *opt != OPT_END) {
        if (*opt == 0) { opt++; continue; }
        if (*opt == OPT_MSG_TYPE) { msg_type = opt[2]; break; }
        opt += 2 + opt[1];
    }

    if (msg_type != DHCPDISCOVER && msg_type != DHCPREQUEST) return;

    // Assign IP: server base + next_client as last octet
    uint8_t client_ip[4] = {
        ip4_addr1(&d->ip), ip4_addr2(&d->ip),
        ip4_addr3(&d->ip), d->next_client,
    };
    if (d->next_client < 254) d->next_client++;

    uint8_t reply = (msg_type == DHCPDISCOVER) ? DHCPOFFER : DHCPACK;
    send_reply(d, pcb, &req, reply, client_ip);
}

// ---- public API -----------------------------------------------------------

void dhcp_server_init(dhcp_server_t *d, ip4_addr_t *ip, ip4_addr_t *nm) {
    d->ip          = *ip;
    d->nm          = *nm;
    d->next_client = 2;

    d->pcb = udp_new();
    if (!d->pcb) return;
    udp_bind(d->pcb, IP_ADDR_ANY, DHCP_SERVER_PORT);
    udp_recv(d->pcb, on_recv, d);
}

void dhcp_server_deinit(dhcp_server_t *d) {
    if (d->pcb) { udp_remove(d->pcb); d->pcb = NULL; }
}
