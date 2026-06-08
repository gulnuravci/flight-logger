#include "wx_fetch.h"
#include "lwip/tcp.h"
#include "lwip/dns.h"
#include "pico/cyw43_arch.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define WX_HOST     "tgftp.nws.noaa.gov"
#define WX_PORT     80
#define BUF_SIZE    512
#define TIMEOUT_MS  10000

// --- Shared state for async TCP callbacks ---

typedef struct {
    struct tcp_pcb *pcb;
    char buf[BUF_SIZE];
    int  buf_len;
    bool done;
    bool error;
    char request[128];
} wx_state_t;

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    wx_state_t *s = (wx_state_t *)arg;
    if (!p) { s->done = true; return ERR_OK; }

    // Copy as much as fits; we only need the first ~200 bytes (METAR is one line)
    for (struct pbuf *q = p; q; q = q->next) {
        int copy = q->len;
        if (s->buf_len + copy >= BUF_SIZE - 1)
            copy = BUF_SIZE - 1 - s->buf_len;
        if (copy > 0) {
            memcpy(s->buf + s->buf_len, q->payload, copy);
            s->buf_len += copy;
        }
    }
    s->buf[s->buf_len] = '\0';
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static err_t on_connected(void *arg, struct tcp_pcb *pcb, err_t err) {
    wx_state_t *s = (wx_state_t *)arg;
    if (err != ERR_OK) { s->error = true; s->done = true; return err; }
    tcp_write(pcb, s->request, strlen(s->request), TCP_WRITE_FLAG_COPY);
    tcp_output(pcb);
    return ERR_OK;
}

static void on_error(void *arg, err_t err) {
    wx_state_t *s = (wx_state_t *)arg;
    s->error = true;
    s->done  = true;
}

static void on_dns(const char *name, const ip_addr_t *addr, void *arg) {
    wx_state_t *s = (wx_state_t *)arg;
    if (!addr) { s->error = true; s->done = true; return; }

    s->pcb = tcp_new();
    tcp_arg(s->pcb, s);
    tcp_recv(s->pcb, on_recv);
    tcp_err(s->pcb, on_error);
    tcp_connect(s->pcb, addr, WX_PORT, on_connected);
}

// --- Parse altimeter from METAR text ---
// Looks for " A" followed by exactly 4 digits (US altimeter, e.g. A3012 = 30.12 inHg)

static int parse_altimeter(const char *metar, float *hpa) {
    const char *p = metar;
    while ((p = strstr(p, " A")) != NULL) {
        p += 2;
        // Verify 4 ASCII digits follow
        if (p[0] >= '2' && p[0] <= '3' &&
            p[1] >= '0' && p[1] <= '9' &&
            p[2] >= '0' && p[2] <= '9' &&
            p[3] >= '0' && p[3] <= '9' &&
            (p[4] == ' ' || p[4] == '\0' || p[4] == '\r' || p[4] == '\n')) {
            float inHg = ((p[0]-'0')*1000 + (p[1]-'0')*100 +
                          (p[2]-'0')*10   + (p[3]-'0')) / 100.0f;
            *hpa = inHg * 33.8639f;
            return 0;
        }
    }
    return -1;
}

// --- Public API ---

int wx_fetch_altimeter(const char *icao_id, float *hpa) {
    static wx_state_t state;
    memset(&state, 0, sizeof(state));

    snprintf(state.request, sizeof(state.request),
        "GET /data/observations/metar/stations/%s.TXT HTTP/1.0\r\n"
        "Host: " WX_HOST "\r\n"
        "Connection: close\r\n\r\n",
        icao_id);

    ip_addr_t addr;
    cyw43_arch_lwip_begin();
    err_t err = dns_gethostbyname(WX_HOST, &addr, on_dns, &state);
    cyw43_arch_lwip_end();

    if (err == ERR_OK) {
        // DNS result was cached — connect directly
        on_dns(WX_HOST, &addr, &state);
    } else if (err != ERR_INPROGRESS) {
        return -1;
    }

    // Poll until done or timeout
    uint32_t start = to_ms_since_boot(get_absolute_time());
    while (!state.done) {
        cyw43_arch_poll();
        cyw43_arch_wait_for_work_until(make_timeout_time_ms(10));
        if (to_ms_since_boot(get_absolute_time()) - start > TIMEOUT_MS) {
            printf("wx_fetch: timeout\n");
            return -1;
        }
    }

    if (state.error || state.buf_len == 0)
        return -1;

    // buf contains the full HTTP response; skip headers to find the METAR
    char *body = strstr(state.buf, "\r\n\r\n");
    if (!body) return -1;
    body += 4;

    return parse_altimeter(body, hpa);
}
