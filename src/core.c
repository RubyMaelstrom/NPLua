#include "core.h"

#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/sem.h"

#include "luaEmbed.h"
#include "telnet.h"

// --- Job passing: core0 -> core1 (Lua chunks) ---

// Internal buffer where we copy the Lua chunk to run
#define NPLUA_MAX_CHUNK   65538

static char luaChunkBuffer[NPLUA_MAX_CHUNK];
static volatile size_t luaChunkLength = 0;
static volatile bool luaRunning = false;

// Semaphore used to signal "new chunk ready" to core1
static semaphore_t luaJobSem;

// --- Output passing: core1 -> core0 (prints etc.) ---

// Simple single-producer (core1) / single-consumer (core0) ring buffer
#define NPLUA_OUTPUT_BUF_SIZE 4096

static char outputBuf[NPLUA_OUTPUT_BUF_SIZE];
static volatile uint32_t outputHead = 0; // write index (core1)
static volatile uint32_t outputTail = 0; // read index (core0)

static inline uint32_t bufMask(uint32_t idx) {
    return idx % NPLUA_OUTPUT_BUF_SIZE;
}

bool npluaIsLuaRunning(void) {
    return luaRunning;
}

// Called from core1 to enqueue text (we append "\r\n" here)
void npluaEnqueueOutput(const char *text) {
    if (!text) return;

    size_t len = strlen(text);
    if (!len) return;

    const char *extra = "\r\n";
    size_t total = len + 2;

    for (size_t i = 0; i < total; i++) {
        char ch = (i < len) ? text[i] : extra[i - len];

        uint32_t head = outputHead;
        uint32_t nextHead = head + 1;

        // If buffer is full, drop the rest of this message
        if ((nextHead - outputTail) > NPLUA_OUTPUT_BUF_SIZE) {
            // Optionally, drop everything or mark overflow
            break;
        }

        outputBuf[bufMask(head)] = ch;
        outputHead = nextHead;
    }
}

// Called from core0 to drain queued output and send via telnet
void npluaDrainOutput(void) {
    while (outputTail != outputHead) {
        char temp[128];
        size_t count = 0;

        while (outputTail != outputHead && count < sizeof(temp) - 1) {
            temp[count++] = outputBuf[bufMask(outputTail)];
            outputTail++;
        }

        temp[count] = '\0';

        // This is safe on core0 — telnet/WiFi live here
        telnetSend(temp);
    }
}

// --- Job submit: core0 asks core1 to run a chunk ---

void npluaQueueChunk(const char *code, size_t length) {
    if (!code || !length) return;

    if (length > NPLUA_MAX_CHUNK) {
        length = NPLUA_MAX_CHUNK;
    }

    memcpy(luaChunkBuffer, code, length);
    luaChunkLength = length;

    // Wake up core1 to handle this chunk
    sem_release(&luaJobSem);
}

// --- Core1 main loop: run Lua jobs ---

static void npluaCore1Main(void) {
    luaInit();

    while (true) {
        sem_acquire_blocking(&luaJobSem);

        size_t len = luaChunkLength;
        if (len > 0 && len <= NPLUA_MAX_CHUNK) {
            luaRunning = true;
            luaRunChunk(luaChunkBuffer, len);
            luaRunning = false;
            luaChunkLength = 0;
        }
    }
}

// --- Public init, called from core0 ---

void npluaInitLuaCore(void) {
    sem_init(&luaJobSem, 0, 1);  // initial count 0

    // Launch core1 to run Lua jobs
    multicore_launch_core1(npluaCore1Main);
}