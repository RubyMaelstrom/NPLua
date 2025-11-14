// telnet.c
#include "telnet.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>

#include "pico/cyw43_arch.h"
#include "console.h"

#include "lwip/tcp.h"
#include "lwip/ip_addr.h"

#define TELNET_PORT      23
#define TELNET_LINE_MAX  256

typedef struct TelnetConn {
    struct tcp_pcb *pcb;
    char lineBuf[TELNET_LINE_MAX];
    int  lineLen;
} TelnetConn;

static TelnetConn *activeConn = NULL;

// Forward declarations for callbacks
static err_t telnetAcceptCb(void *arg, struct tcp_pcb *newPcb, err_t err);
static err_t telnetRecvCb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
static void  telnetErrorCb(void *arg, err_t err);
static err_t telnetSentCb(void *arg, struct tcp_pcb *tpcb, u16_t len);

static void telnetClose(TelnetConn *conn) {
    if (!conn) return;

    if (conn->pcb) {
        tcp_arg(conn->pcb, NULL);
        tcp_recv(conn->pcb, NULL);
        tcp_err(conn->pcb, NULL);
        tcp_sent(conn->pcb, NULL);
        tcp_poll(conn->pcb, NULL, 0);

        tcp_close(conn->pcb);  // ignore error for now
        conn->pcb = NULL;
    }

    if (activeConn == conn) {
        activeConn = NULL;
    }

    free(conn);
}

void telnetSend(const char *text) {
    if (!text || !activeConn || !activeConn->pcb) return;

    size_t len = strlen(text);
    if (!len) return;

    const char *p = text;

    while (len > 0) {
        u16_t chunk = (len > TCP_MSS) ? TCP_MSS : (u16_t)len;

        err_t err = tcp_write(activeConn->pcb, p, chunk, TCP_WRITE_FLAG_COPY);
        if (err != ERR_OK) {
            // If buffer is full, try to flush and stop
            tcp_output(activeConn->pcb);
            break;
        }

        p   += chunk;
        len -= chunk;
    }

    tcp_output(activeConn->pcb);
}

// console callbacks
static void consoleWriteCallback(const char *text) {
    telnetSend(text);
}

static void consolePrintfCallback(const char *fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    telnetSend(buf);
}

static err_t telnetRecvCb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    TelnetConn *conn = (TelnetConn *)arg;

    if (!conn) {
        if (p) {
            tcp_recved(tpcb, p->tot_len);
            pbuf_free(p);
        }
        return ERR_OK;
    }

    if (!p) {
        // remote closed
        telnetClose(conn);
        return ERR_OK;
    }

    if (err != ERR_OK) {
        tcp_recved(tpcb, p->tot_len);
        pbuf_free(p);
        return err;
    }

    struct pbuf *q = p;
    while (q) {
        const char *data = (const char *)q->payload;
        for (u16_t i = 0; i < q->len; i++) {
            char c = data[i];

            if (c == '\r') {
                continue;
            }

            if (c == '\n') {
                conn->lineBuf[conn->lineLen] = '\0';

                consoleHandleLine(conn->lineBuf);

                const char *prompt = consoleGetPrompt();
                if (prompt) {
                    telnetSend(prompt);
                }

                conn->lineLen = 0;
                continue;
            }

            if (conn->lineLen < (TELNET_LINE_MAX - 1)) {
                conn->lineBuf[conn->lineLen++] = c;
            }
        }
        q = q->next;
    }

    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);

    return ERR_OK;
}

static void telnetErrorCb(void *arg, err_t err) {
    (void)err;
    TelnetConn *conn = (TelnetConn *)arg;
    if (conn) {
        telnetClose(conn);
    }
}

static err_t telnetSentCb(void *arg, struct tcp_pcb *tpcb, u16_t len) {
    (void)arg;
    (void)tpcb;
    (void)len;
    return ERR_OK;
}

static err_t telnetAcceptCb(void *arg, struct tcp_pcb *newPcb, err_t err) {
    (void)arg;
    if (err != ERR_OK || !newPcb) {
        return ERR_VAL;
    }

    // only one connection at a time
    if (activeConn) {
        tcp_abort(newPcb);
        return ERR_ABRT;
    }

    TelnetConn *conn = (TelnetConn *)calloc(1, sizeof(TelnetConn));
    if (!conn) {
        tcp_abort(newPcb);
        return ERR_MEM;
    }

    conn->pcb = newPcb;
    conn->lineLen = 0;
    activeConn = conn;

    tcp_arg(newPcb, conn);
    tcp_recv(newPcb, telnetRecvCb);
    tcp_err(newPcb, telnetErrorCb);
    tcp_sent(newPcb, telnetSentCb);

    // Init console for this new session
    consoleInit(consoleWriteCallback, consolePrintfCallback);

    const char *prompt = consoleGetPrompt();
    if (prompt) {
        telnetSend(prompt);
    }

    return ERR_OK;
}

void telnetInit(void) {
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (!pcb) {
        printf("telnet: tcp_new_ip_type failed\r\n");
        return;
    }

    err_t err = tcp_bind(pcb, IP_ANY_TYPE, TELNET_PORT);
    if (err != ERR_OK) {
        printf("telnet: tcp_bind failed: %d\r\n", err);
        tcp_close(pcb);
        return;
    }

    pcb = tcp_listen_with_backlog(pcb, 1);
    if (!pcb) {
        printf("telnet: tcp_listen failed\r\n");
        return;
    }

    tcp_accept(pcb, telnetAcceptCb);
    printf("telnet: listening on port %d (raw API)\r\n", TELNET_PORT);
}

void telnetCloseActive(void) {
    if(activeConn) {
        telnetClose(activeConn);
    }
}