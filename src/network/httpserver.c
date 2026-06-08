#include "httpserver.h"
#include "lwip/tcp.h"
#include "pico/cyw43_arch.h"
#include "ff.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Minimal HTTP/1.0 server using lwIP raw TCP.
//
// Key design: FatFS (SD card SPI) must NOT be called from the lwIP IRQ
// callback context — blocking SPI from an IRQ is unsafe on the Pico W.
// Solution: on_recv() only sets a flag when headers are complete.
//           httpserver_poll() (called from the main loop) does all FatFS
//           work and drives file streaming.
// ---------------------------------------------------------------------------

#define HTTP_PORT        80
#define REQ_BUF_SIZE    1024  // JS fetch() sends larger headers than direct navigation
#define FILE_CHUNK_SIZE 512

static const char *CORS =
    "Access-Control-Allow-Origin: *\r\n"
    "Access-Control-Allow-Methods: GET, OPTIONS\r\n"
    "Access-Control-Allow-Headers: Content-Type\r\n"
    "Access-Control-Allow-Private-Network: true\r\n";

// ---- connection state -------------------------------------------------------

typedef struct {
    struct tcp_pcb *pcb;
    char            req[REQ_BUF_SIZE];
    int             req_len;
    bool            headers_complete; // set by on_recv when \r\n\r\n seen
    bool            dispatched;       // set by httpserver_poll after dispatch()
    bool            streaming;        // file is open and being sent in chunks
    FIL             file;
    bool            file_open;
    uint8_t         chunk[FILE_CHUNK_SIZE];
} http_conn_t;

static http_conn_t s_conn;

// ---- helpers ----------------------------------------------------------------

static void tcp_send(struct tcp_pcb *pcb, const char *s) {
    tcp_write(pcb, s, (u16_t)strlen(s), TCP_WRITE_FLAG_COPY);
}

// Close the connection and reset state. Call with lwIP lock held.
static void close_conn(http_conn_t *conn) {
    if (conn->file_open) {
        f_close(&conn->file);
        conn->file_open = false;
    }
    conn->streaming        = false;
    conn->dispatched       = false;
    conn->headers_complete = false;
    conn->req_len          = 0;
    if (conn->pcb) {
        tcp_arg(conn->pcb,  NULL);
        tcp_recv(conn->pcb, NULL);
        tcp_sent(conn->pcb, NULL);
        tcp_err(conn->pcb,  NULL);
        tcp_close(conn->pcb);
        conn->pcb = NULL;
    }
}

// ---- API handlers (called from main loop — FatFS safe) ---------------------

static void handle_flights(struct tcp_pcb *pcb) {
    static char json[2048];
    int pos = 0;
    pos += snprintf(json + pos, (int)sizeof(json) - pos, "[");

    DIR dir;
    FILINFO fno;
    bool first = true;

    if (f_opendir(&dir, "") == FR_OK) {
        while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0]) {
            if (strncmp(fno.fname, "FLT_", 4) != 0) continue;
            if (!strstr(fno.fname, ".CSV"))          continue;

            int      id   = atoi(fno.fname + 4);
            uint32_t rows = fno.fsize > 100 ? (uint32_t)((fno.fsize - 100) / 65) : 0;
            float    dur  = (float)rows * 0.1f;

            if (!first) pos += snprintf(json + pos, (int)sizeof(json) - pos, ",");
            pos += snprintf(json + pos, (int)sizeof(json) - pos,
                "{\"id\":%d,\"filename\":\"%s\",\"rows\":%lu,\"duration_s\":%.1f}",
                id, fno.fname, (unsigned long)rows, dur);
            first = false;
        }
        f_closedir(&dir);
    }
    pos += snprintf(json + pos, (int)sizeof(json) - pos, "]");

    static char hdr[256];
    snprintf(hdr, sizeof(hdr),
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "%s\r\n", pos, CORS);

    tcp_send(pcb, hdr);
    tcp_write(pcb, json, (u16_t)pos, TCP_WRITE_FLAG_COPY);
    tcp_output(pcb);
}

static void handle_flight(struct tcp_pcb *pcb, http_conn_t *conn, int id) {
    char fname[16];
    snprintf(fname, sizeof(fname), "FLT_%03d.CSV", id);

    if (f_open(&conn->file, fname, FA_READ) != FR_OK) {
        tcp_send(pcb,
            "HTTP/1.0 404 Not Found\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 9\r\n"
            "Connection: close\r\n"
            "\r\nNot found");
        tcp_output(pcb);
        return;
    }
    conn->file_open = true;

    static char hdr[256];
    snprintf(hdr, sizeof(hdr),
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: text/csv\r\n"
        "Content-Length: %lu\r\n"
        "Connection: close\r\n"
        "%s\r\n",
        (unsigned long)f_size(&conn->file), CORS);

    tcp_send(pcb, hdr);
    tcp_output(pcb);
    conn->streaming = true;
}

// ---- dispatch (called from main loop after headers are complete) -----------

static void dispatch(http_conn_t *conn) {
    struct tcp_pcb *pcb = conn->pcb;
    char *req = conn->req;

    if (strncmp(req, "OPTIONS", 7) == 0) {
        tcp_send(pcb,
            "HTTP/1.0 204 No Content\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type\r\n"
            "Access-Control-Allow-Private-Network: true\r\n"
            "Connection: close\r\n"
            "\r\n");
        tcp_output(pcb);
        return;
    }

    if (strncmp(req, "GET ", 4) != 0) {
        tcp_send(pcb, "HTTP/1.0 405 Method Not Allowed\r\n\r\n");
        tcp_output(pcb);
        return;
    }

    char *path = req + 4;
    char *sp   = strpbrk(path, " \r\n");
    if (sp) *sp = '\0';

    if (strcmp(path, "/api/flights") == 0) {
        handle_flights(pcb);
    } else if (strncmp(path, "/api/flight/", 12) == 0) {
        int id = atoi(path + 12);
        handle_flight(pcb, conn, id);
    } else {
        tcp_send(pcb,
            "HTTP/1.0 404 Not Found\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 9\r\n\r\nNot found");
        tcp_output(pcb);
    }
}

// ---- TCP callbacks (no FatFS here) -----------------------------------------

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    http_conn_t *conn = (http_conn_t *)arg;

    if (!p) { close_conn(conn); return ERR_OK; }
    if (err != ERR_OK) { pbuf_free(p); return err; }

    for (struct pbuf *q = p; q; q = q->next) {
        int copy = (int)q->len;
        if (conn->req_len + copy >= REQ_BUF_SIZE - 1)
            copy = REQ_BUF_SIZE - 1 - conn->req_len;
        if (copy > 0) {
            memcpy(conn->req + conn->req_len, q->payload, (size_t)copy);
            conn->req_len += copy;
        }
    }
    conn->req[conn->req_len] = '\0';
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);

    // Just set the flag — dispatch runs in httpserver_poll() (main loop)
    if (!conn->headers_complete &&
        (strstr(conn->req, "\r\n\r\n") || strstr(conn->req, "\n\n")))
    {
        conn->headers_complete = true;
    }
    return ERR_OK;
}

static err_t on_sent(void *arg, struct tcp_pcb *pcb, u16_t len) {
    http_conn_t *conn = (http_conn_t *)arg;
    (void)len;
    // Non-streaming responses: close after all data ACKed.
    // Streaming responses are closed by httpserver_poll() when the file ends.
    if (!conn->streaming) {
        close_conn(conn);
    }
    return ERR_OK;
}

static void on_error(void *arg, err_t err) {
    http_conn_t *conn = (http_conn_t *)arg;
    (void)err;
    if (conn->file_open) { f_close(&conn->file); conn->file_open = false; }
    conn->streaming        = false;
    conn->dispatched       = false;
    conn->headers_complete = false;
    conn->req_len          = 0;
    conn->pcb              = NULL;
}

static err_t on_accept(void *arg, struct tcp_pcb *client, err_t err) {
    (void)arg;
    if (err != ERR_OK || !client) return ERR_VAL;

    if (s_conn.pcb) { tcp_abort(client); return ERR_ABRT; }

    memset(&s_conn, 0, sizeof(s_conn));
    s_conn.pcb = client;

    tcp_arg(client, &s_conn);
    tcp_recv(client, on_recv);
    tcp_sent(client, on_sent);
    tcp_err(client, on_error);

    return ERR_OK;
}

// ---- public API -------------------------------------------------------------

void httpserver_init(void) {
    memset(&s_conn, 0, sizeof(s_conn));

    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb) return;
    if (tcp_bind(pcb, IP_ADDR_ANY, HTTP_PORT) != ERR_OK) return;
    pcb = tcp_listen(pcb);
    if (!pcb) return;
    tcp_accept(pcb, on_accept);
}

void httpserver_poll(void) {
    // Step 1: dispatch request once headers are complete (FatFS safe — main loop)
    if (s_conn.headers_complete && !s_conn.dispatched && s_conn.pcb) {
        s_conn.dispatched = true;
        cyw43_arch_lwip_begin();
        dispatch(&s_conn);
        cyw43_arch_lwip_end();
    }

    // Step 2: stream file data chunk by chunk
    if (!s_conn.streaming || !s_conn.pcb || !s_conn.file_open) return;

    cyw43_arch_lwip_begin();
    u16_t avail = tcp_sndbuf(s_conn.pcb);
    cyw43_arch_lwip_end();

    if (avail < FILE_CHUNK_SIZE) return;

    UINT br;
    FRESULT fr = f_read(&s_conn.file, s_conn.chunk, FILE_CHUNK_SIZE, &br);

    if (fr != FR_OK || br == 0) {
        // EOF — close file first (FatFS must be outside lwIP lock), then close TCP.
        // IMPORTANT: do NOT set streaming=false before acquiring the lock — if
        // on_sent fires in that window it would call close_conn → f_close from
        // IRQ context (unsafe SPI) and double-close the PCB.
        f_close(&s_conn.file);
        s_conn.file_open = false;

        cyw43_arch_lwip_begin();
        s_conn.streaming = false;   // set inside lock so on_sent can't race us
        if (s_conn.pcb) {
            struct tcp_pcb *pcb = s_conn.pcb;
            s_conn.pcb = NULL;
            tcp_arg(pcb,  NULL);
            tcp_recv(pcb, NULL);
            tcp_sent(pcb, NULL);
            tcp_err(pcb,  NULL);
            tcp_close(pcb);
        }
        cyw43_arch_lwip_end();
        return;
    }

    cyw43_arch_lwip_begin();
    tcp_write(s_conn.pcb, s_conn.chunk, (u16_t)br, TCP_WRITE_FLAG_COPY);
    tcp_output(s_conn.pcb);
    cyw43_arch_lwip_end();
}
