// telnet.c
#include "telnet.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#include "pico/cyw43_arch.h"
#include "nplua_config.h"
#include "console.h"

#include "lwip/tcp.h"
#include "lwip/ip_addr.h"

enum {
    TELNET_IAC  = 255,
    TELNET_DONT = 254,
    TELNET_DO   = 253,
    TELNET_WONT = 252,
    TELNET_WILL = 251,
    TELNET_SB   = 250,
    TELNET_SE   = 240
};

typedef enum TelnetParseState {
    TelnetParseData,
    TelnetParseIac,
    TelnetParseOption,
    TelnetParseSubnegotiation,
    TelnetParseSubnegotiationIac
} TelnetParseState;

typedef struct TelnetConn {
    struct tcp_pcb *pcb;
    char lineBuf[NPLUA_TELNET_LINE_SIZE];
    uint8_t txBuf[NPLUA_TELNET_TX_BUFFER_SIZE];
    size_t lineLen;
    size_t txHead;
    size_t txTail;
    size_t txCount;
    bool closeRequested;
    bool lineOverflow;
    TelnetParseState parseState;
    uint8_t optionCommand;
} TelnetConn;

_Static_assert(NPLUA_TELNET_LINE_SIZE >= 2,
               "NPLUA_TELNET_LINE_SIZE must be at least 2");
_Static_assert(NPLUA_TELNET_TX_BUFFER_SIZE >= 64,
               "NPLUA_TELNET_TX_BUFFER_SIZE must be at least 64");

static TelnetConn connection;
static TelnetConn *activeConn = NULL;

// Forward declarations for callbacks
static err_t telnetAcceptCb(void *arg, struct tcp_pcb *newPcb, err_t err);
static err_t telnetRecvCb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
static void  telnetErrorCb(void *arg, err_t err);
static err_t telnetSentCb(void *arg, struct tcp_pcb *tpcb, u16_t len);

static void telnetFlushLocked(TelnetConn *conn) {
    if (!conn || !conn->pcb) return;

    bool wrote = false;
    while (conn->txCount > 0) {
        u16_t available = tcp_sndbuf(conn->pcb);
        if (available == 0) break;

        size_t contiguous = NPLUA_TELNET_TX_BUFFER_SIZE - conn->txTail;
        size_t count = conn->txCount;
        if (count > contiguous) count = contiguous;
        if (count > available) count = available;

        err_t err = tcp_write(conn->pcb, &conn->txBuf[conn->txTail],
                              (u16_t)count, TCP_WRITE_FLAG_COPY);
        if (err != ERR_OK) break;

        conn->txTail = (conn->txTail + count) % NPLUA_TELNET_TX_BUFFER_SIZE;
        conn->txCount -= count;
        wrote = true;
    }

    if (wrote) {
        (void)tcp_output(conn->pcb);
    }
}

static size_t telnetQueueBytesLocked(const void *data, size_t length) {
    if (!data || !activeConn || !activeConn->pcb) return 0;

    TelnetConn *conn = activeConn;
    const uint8_t *bytes = (const uint8_t *)data;
    size_t queued = 0;

    telnetFlushLocked(conn);
    while (queued < length && conn->txCount < NPLUA_TELNET_TX_BUFFER_SIZE) {
        size_t freeSpace = NPLUA_TELNET_TX_BUFFER_SIZE - conn->txCount;
        size_t contiguous = NPLUA_TELNET_TX_BUFFER_SIZE - conn->txHead;
        size_t count = length - queued;
        if (count > freeSpace) count = freeSpace;
        if (count > contiguous) count = contiguous;

        memcpy(&conn->txBuf[conn->txHead], &bytes[queued], count);
        conn->txHead = (conn->txHead + count) % NPLUA_TELNET_TX_BUFFER_SIZE;
        conn->txCount += count;
        queued += count;
    }

    telnetFlushLocked(conn);
    return queued;
}

static err_t telnetCloseLocked(TelnetConn *conn) {
    if (!conn) return ERR_OK;

    struct tcp_pcb *pcb = conn->pcb;

    telnetFlushLocked(conn);

    if (activeConn == conn) {
        activeConn = NULL;
    }
    memset(conn, 0, sizeof(*conn));

    if (!pcb) {
        return ERR_OK;
    }

    tcp_arg(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_err(pcb, NULL);
    tcp_sent(pcb, NULL);
    tcp_poll(pcb, NULL, 0);

    err_t err = tcp_close(pcb);
    if (err != ERR_OK) {
        tcp_abort(pcb);
        return ERR_ABRT;
    }

    return ERR_OK;
}

static void telnetSendLocked(const char *text) {
    if (!text) return;
    (void)telnetQueueBytesLocked(text, strlen(text));
}

void telnetSend(const char *text) {
    cyw43_arch_lwip_begin();
    telnetSendLocked(text);
    cyw43_arch_lwip_end();
}

void telnetSendBytes(const void *data, size_t length) {
    cyw43_arch_lwip_begin();
    (void)telnetQueueBytesLocked(data, length);
    cyw43_arch_lwip_end();
}

void telnetSendLine(const char *text) {
    if (!text) text = "";

    cyw43_arch_lwip_begin();
    telnetSendLocked(text);
    (void)telnetQueueBytesLocked("\r\n", 2);
    cyw43_arch_lwip_end();
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

static void telnetHandleDataByteLocked(TelnetConn *conn, uint8_t byte) {
    if (byte == '\r' || byte == '\0') {
        return;
    }

    if (byte == '\b' || byte == 0x7f) {
        if (!conn->lineOverflow && conn->lineLen > 0) {
            conn->lineLen--;
        }
        return;
    }

    if (byte == '\n') {
        if (conn->lineOverflow) {
            telnetSendLocked("Input line too long; line discarded.\r\n");
        } else {
            conn->lineBuf[conn->lineLen] = '\0';
            conn->lineLen = 0;
            consoleHandleLine(conn->lineBuf);

            if (!conn->closeRequested) {
                const char *prompt = consoleGetPrompt();
                if (prompt) {
                    telnetSendLocked(prompt);
                }
            }
        }

        conn->lineLen = 0;
        conn->lineOverflow = false;
        return;
    }

    if (conn->lineLen < NPLUA_TELNET_LINE_SIZE - 1) {
        conn->lineBuf[conn->lineLen++] = (char)byte;
    } else {
        conn->lineOverflow = true;
    }
}

static void telnetHandleByteLocked(TelnetConn *conn, uint8_t byte) {
    switch (conn->parseState) {
        case TelnetParseData:
            if (byte == TELNET_IAC) {
                conn->parseState = TelnetParseIac;
            } else {
                telnetHandleDataByteLocked(conn, byte);
            }
            break;

        case TelnetParseIac:
            if (byte == TELNET_IAC) {
                conn->parseState = TelnetParseData;
                telnetHandleDataByteLocked(conn, byte);
            } else if (byte == TELNET_WILL || byte == TELNET_WONT ||
                       byte == TELNET_DO || byte == TELNET_DONT) {
                conn->optionCommand = byte;
                conn->parseState = TelnetParseOption;
            } else if (byte == TELNET_SB) {
                conn->parseState = TelnetParseSubnegotiation;
            } else {
                conn->parseState = TelnetParseData;
            }
            break;

        case TelnetParseOption:
            if (conn->optionCommand == TELNET_WILL ||
                conn->optionCommand == TELNET_DO) {
                uint8_t response[3] = {
                    TELNET_IAC,
                    conn->optionCommand == TELNET_WILL ? TELNET_DONT : TELNET_WONT,
                    byte
                };
                (void)telnetQueueBytesLocked(response, sizeof(response));
            }
            conn->parseState = TelnetParseData;
            break;

        case TelnetParseSubnegotiation:
            if (byte == TELNET_IAC) {
                conn->parseState = TelnetParseSubnegotiationIac;
            }
            break;

        case TelnetParseSubnegotiationIac:
            conn->parseState = byte == TELNET_SE
                ? TelnetParseData
                : TelnetParseSubnegotiation;
            break;
    }
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
        return telnetCloseLocked(conn);
    }

    if (err != ERR_OK) {
        tcp_recved(tpcb, p->tot_len);
        pbuf_free(p);
        return err;
    }

    struct pbuf *q = p;
    while (q) {
        const uint8_t *data = (const uint8_t *)q->payload;
        for (u16_t i = 0; i < q->len; i++) {
            telnetHandleByteLocked(conn, data[i]);
            if (conn->closeRequested) break;
        }

        if (conn->closeRequested) {
            break;
        }
        q = q->next;
    }

    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);

    if (conn->closeRequested) {
        return telnetCloseLocked(conn);
    }

    return ERR_OK;
}

static void telnetErrorCb(void *arg, err_t err) {
    (void)err;
    TelnetConn *conn = (TelnetConn *)arg;
    if (conn) {
        // lwIP has already freed the PCB before invoking the error callback.
        if (activeConn == conn) {
            activeConn = NULL;
        }
        memset(conn, 0, sizeof(*conn));
    }
}

static err_t telnetSentCb(void *arg, struct tcp_pcb *tpcb, u16_t len) {
    (void)tpcb;
    (void)len;

    telnetFlushLocked((TelnetConn *)arg);
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

    TelnetConn *conn = &connection;
    memset(conn, 0, sizeof(*conn));

    conn->pcb = newPcb;
    conn->lineLen = 0;
    activeConn = conn;

    tcp_arg(newPcb, conn);
    tcp_recv(newPcb, telnetRecvCb);
    tcp_err(newPcb, telnetErrorCb);
    tcp_sent(newPcb, telnetSentCb);
    tcp_nagle_disable(newPcb);

    // Init console for this new session
    consoleInit(consoleWriteCallback, consolePrintfCallback);

    return ERR_OK;
}

void telnetInit(void) {
    cyw43_arch_lwip_begin();

    // One dual-stack listener accepts connections over both IPv4 and IPv6.
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb) {
        printf("telnet: tcp_new_ip_type failed\r\n");
        cyw43_arch_lwip_end();
        return;
    }

    err_t err = tcp_bind(pcb, IP_ANY_TYPE, NPLUA_TELNET_PORT);
    if (err != ERR_OK) {
        printf("telnet: tcp_bind failed: %d\r\n", err);
        tcp_close(pcb);
        cyw43_arch_lwip_end();
        return;
    }

    pcb = tcp_listen_with_backlog(pcb, 1);
    if (!pcb) {
        printf("telnet: tcp_listen failed\r\n");
        cyw43_arch_lwip_end();
        return;
    }

    tcp_accept(pcb, telnetAcceptCb);
    printf("telnet: listening on port %d over IPv4 and IPv6 (raw API)\r\n",
           NPLUA_TELNET_PORT);

    cyw43_arch_lwip_end();
}

void telnetCloseActive(void) {
    cyw43_arch_lwip_begin();
    if (activeConn) {
        activeConn->closeRequested = true;
    }
    cyw43_arch_lwip_end();
}

void telnetProcess(void) {
    cyw43_arch_lwip_begin();
    if (activeConn && activeConn->closeRequested) {
        (void)telnetCloseLocked(activeConn);
    }
    cyw43_arch_lwip_end();
}
