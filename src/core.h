#pragma once

#include <stddef.h>
#include <stdbool.h>

// Called from core0 during startup, before the main loop
void npluaInitLuaCore(void);

// Called from core0 when the user finishes a Lua upload (:done)
void npluaQueueChunk(const char *code, size_t length);

// Called from core1 (Lua side) to enqueue lines of output
void npluaEnqueueOutput(const char *text);

// Called from core0 in the main loop to flush Lua output over telnet
void npluaDrainOutput(void);

bool npluaIsLuaRunning(void);