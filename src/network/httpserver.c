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
#define FILE_CHUNK_SIZE 2048  // larger chunks → fewer FatFS reads per second

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

#define MAX_CONNS 4
static http_conn_t s_conns[MAX_CONNS];

static http_conn_t *alloc_conn(void) {
    for (int i = 0; i < MAX_CONNS; i++) {
        if (!s_conns[i].pcb) return &s_conns[i];
    }
    return NULL;
}

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

// ---- MIME type helper -------------------------------------------------------

static const char *mime_type(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0) return "text/html; charset=utf-8";
    if (strcmp(dot, ".js")   == 0) return "application/javascript";
    if (strcmp(dot, ".css")  == 0) return "text/css";
    if (strcmp(dot, ".svg")  == 0) return "image/svg+xml";
    if (strcmp(dot, ".ico")  == 0) return "image/x-icon";
    if (strcmp(dot, ".json") == 0) return "application/json";
    return "application/octet-stream";
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

// Serve a file from the SD card "app/" directory.
// URL path "/" and any path without an extension (React Router routes like
// "/flight/001") fall back to "app/index.html" so client-side routing works.
static void handle_static(struct tcp_pcb *pcb, http_conn_t *conn,
                           const char *url_path) {
    char fpath[128];

    // Determine whether the URL looks like a real file (has an extension).
    const char *dot   = strrchr(url_path, '.');
    const char *slash = strrchr(url_path, '/');
    bool has_ext = dot && (!slash || dot > slash);

    if (!has_ext || strcmp(url_path, "/") == 0) {
        // SPA route or bare "/" → serve the app shell from SD root
        snprintf(fpath, sizeof(fpath), "index.htm");
    } else {
        // Strip leading "/" — FatFS paths are relative to root, no leading slash
        snprintf(fpath, sizeof(fpath), "%s", url_path + 1);
    }

    if (f_open(&conn->file, fpath, FA_READ) != FR_OK) {
        // File not found → SPA fallback to index.html
        if (strcmp(fpath, "index.htm") == 0 ||
            f_open(&conn->file, "index.htm", FA_READ) != FR_OK) {
            tcp_send(pcb,
                "HTTP/1.0 404 Not Found\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: 9\r\n"
                "Connection: close\r\n"
                "\r\nNot found");
            tcp_output(pcb);
            return;
        }
        snprintf(fpath, sizeof(fpath), "index.htm");
    }
    conn->file_open = true;

    static char hdr[256];
    snprintf(hdr, sizeof(hdr),
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %lu\r\n"
        "Connection: close\r\n"
        "\r\n",
        mime_type(fpath), (unsigned long)f_size(&conn->file));
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
        // Serve the built PWA from the SD card "app/" directory.
        // Falls back to app/index.html for React Router client-side routes.
        handle_static(pcb, conn, path);
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

    http_conn_t *conn = alloc_conn();
    if (!conn) { tcp_abort(client); return ERR_ABRT; }

    memset(conn, 0, sizeof(*conn));
    conn->pcb = client;

    tcp_arg(client, conn);
    tcp_recv(client, on_recv);
    tcp_sent(client, on_sent);
    tcp_err(client, on_error);

    return ERR_OK;
}

// ---- public API -------------------------------------------------------------

void httpserver_init(void) {
    memset(s_conns, 0, sizeof(s_conns));

    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb) return;
    if (tcp_bind(pcb, IP_ADDR_ANY, HTTP_PORT) != ERR_OK) return;
    pcb = tcp_listen(pcb);
    if (!pcb) return;
    tcp_accept(pcb, on_accept);
}

void httpserver_poll(void) {
    for (int i = 0; i < MAX_CONNS; i++) {
        http_conn_t *conn = &s_conns[i];
        if (!conn->pcb) continue;

        // Step 1: dispatch once headers are complete (FatFS safe — main loop)
        if (conn->headers_complete && !conn->dispatched) {
            conn->dispatched = true;
            cyw43_arch_lwip_begin();
            dispatch(conn);
            cyw43_arch_lwip_end();
        }

        // Step 2: stream file data chunk by chunk
        if (!conn->streaming || !conn->file_open) continue;

        cyw43_arch_lwip_begin();
        u16_t avail = tcp_sndbuf(conn->pcb);
        cyw43_arch_lwip_end();

        if (avail < FILE_CHUNK_SIZE) continue;

        UINT br;
        FRESULT fr = f_read(&conn->file, conn->chunk, FILE_CHUNK_SIZE, &br);

        if (fr != FR_OK || br == 0) {
            // EOF — close file first (FatFS outside lwIP lock), then close TCP.
            f_close(&conn->file);
            conn->file_open = false;

            cyw43_arch_lwip_begin();
            conn->streaming = false;
            if (conn->pcb) {
                struct tcp_pcb *pcb = conn->pcb;
                conn->pcb = NULL;
                tcp_arg(pcb,  NULL);
                tcp_recv(pcb, NULL);
                tcp_sent(pcb, NULL);
                tcp_err(pcb,  NULL);
                tcp_close(pcb);
            }
            cyw43_arch_lwip_end();
            continue;
        }

        cyw43_arch_lwip_begin();
        tcp_write(conn->pcb, conn->chunk, (u16_t)br, TCP_WRITE_FLAG_COPY);
        tcp_output(conn->pcb);
        cyw43_arch_lwip_end();
    }
}
