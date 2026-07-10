#pragma once

#include <stddef.h>
#include <stdbool.h>

#define NPLUA_MAX_CHUNK_SIZE (64u * 1024u)

// Called from core0 during startup, before the main loop
void npluaInitLuaCore(void);

// Called from core0 when the user finishes a Lua upload (:done). The caller
// must keep code unchanged until npluaIsLuaRunning() becomes false.
bool npluaQueueChunk(const char *code, size_t length);

bool npluaIsLuaRunning(void);

// Returns true once for each completed Lua job.
bool npluaTakeLuaCompletion(void);
