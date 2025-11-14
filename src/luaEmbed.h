// luaEmbed.h
#pragma once

#include <stddef.h>

// Called at startup
void luaInit(void);

// Called when we have a Lua chunk from the console
void luaRunChunk(const char *code, size_t length);