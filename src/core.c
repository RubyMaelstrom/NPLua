#include "core.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

#include "pico/multicore.h"
#include "pico/sem.h"

#include "nplua_config.h"
#include "luaEmbed.h"
// --- Job passing: core0 -> core1 (Lua chunks) ---

static const char *luaChunk = NULL;
static size_t luaChunkLength = 0;
static atomic_bool luaJobActive = ATOMIC_VAR_INIT(false);
static atomic_bool luaJobCompleted = ATOMIC_VAR_INIT(false);

// Semaphore used to signal "new chunk ready" to core1
static semaphore_t luaJobSem;

_Static_assert(NPLUA_CORE1_STACK_SIZE >= 2048,
               "NPLUA_CORE1_STACK_SIZE must be at least 2048 bytes");
_Static_assert(NPLUA_CORE1_STACK_SIZE % sizeof(uint32_t) == 0,
               "NPLUA_CORE1_STACK_SIZE must be word aligned");

static uint32_t core1Stack[NPLUA_CORE1_STACK_SIZE / sizeof(uint32_t)];

bool npluaIsLuaRunning(void) {
    return atomic_load_explicit(&luaJobActive, memory_order_acquire);
}

bool npluaTakeLuaCompletion(void) {
    return atomic_exchange_explicit(
        &luaJobCompleted, false, memory_order_acq_rel);
}

// --- Job submit: core0 asks core1 to run a chunk ---

bool npluaQueueChunk(const char *code, size_t length) {
    if (!code || !length || length > NPLUA_MAX_CHUNK_SIZE) {
        return false;
    }

    bool expected = false;
    if (!atomic_compare_exchange_strong_explicit(
            &luaJobActive, &expected, true,
            memory_order_acq_rel, memory_order_acquire)) {
        return false;
    }

    luaChunk = code;
    luaChunkLength = length;

    // Publish the pointer and length before releasing the semaphore.
    atomic_thread_fence(memory_order_release);

    // Wake up core1 to handle this chunk
    sem_release(&luaJobSem);
    return true;
}

// --- Core1 main loop: run Lua jobs ---

static void npluaCore1Main(void) {
    luaInit();

    while (true) {
        sem_acquire_blocking(&luaJobSem);
        atomic_thread_fence(memory_order_acquire);

        const char *code = luaChunk;
        size_t len = luaChunkLength;
        if (code && len > 0 && len <= NPLUA_MAX_CHUNK_SIZE) {
            luaRunChunk(code, len);
        }

        luaChunk = NULL;
        luaChunkLength = 0;
        atomic_store_explicit(&luaJobActive, false, memory_order_release);
        atomic_store_explicit(&luaJobCompleted, true, memory_order_release);
    }
}

// --- Public init, called from core0 ---

void npluaInitLuaCore(void) {
    sem_init(&luaJobSem, 0, 1);  // initial count 0

    // Launch core1 to run Lua jobs
    multicore_launch_core1_with_stack(
        npluaCore1Main, core1Stack, sizeof(core1Stack));
}
