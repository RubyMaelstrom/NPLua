// luaEmbed.c
#include "luaEmbed.h"
#include "gpio.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include "nplua_config.h"
#include "telnet.h"
#include "pico/time.h"
#include "pico/cyw43_arch.h"

// Lua headers – you need these in your include path
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

static lua_State *luaState = NULL;

typedef struct LuaAllocator {
    size_t used;
} LuaAllocator;

static LuaAllocator luaAllocatorState;

_Static_assert(NPLUA_LUA_HEAP_LIMIT >= 32768,
               "NPLUA_LUA_HEAP_LIMIT must be at least 32768 bytes");

static void luaOutputLine(const char *text);

static int luaPanic(lua_State *L) {
    const char *message = lua_tostring(L, -1);
    luaOutputLine(message ? message : "Unprotected Lua error");
    return 0;
}

static void *luaAllocator(void *userData, void *ptr, size_t oldSize,
                          size_t newSize) {
    LuaAllocator *allocator = (LuaAllocator *)userData;

    if (!ptr) {
        oldSize = 0;
    }

    if (newSize == 0) {
        free(ptr);
        allocator->used = oldSize <= allocator->used
            ? allocator->used - oldSize
            : 0;
        return NULL;
    }

    if (newSize > oldSize &&
        newSize - oldSize > NPLUA_LUA_HEAP_LIMIT - allocator->used) {
        return NULL;
    }

    void *newPtr = realloc(ptr, newSize);
    if (!newPtr) {
        return NULL;
    }

    allocator->used = allocator->used - oldSize + newSize;
    return newPtr;
}

// Small helper: send output to telnet (and also to USB for debug)
static void luaOutputLine(const char *text) {
    if (!text) text = "";

    telnetSendLine(text);

    // Also log to USB for debug
    printf("%s\n", text);
}

// Replacement for Lua's global print()
static int luaNpluaPrint(lua_State *L) {
    int top = lua_gettop(L);
    if (top == 0) {
        luaOutputLine("");
        return 0;
    }

    for (int i = 1; i <= top; i++) {
        size_t len;
        const char *s = luaL_tolstring(L, i, &len); // converts to string, leaves result on stack

        if (!s) {
            s = "";
            len = 0;
        }

        if (i > 1) {
            telnetSendBytes("\t", 1);
            putchar('\t');
        }

        if (len > 0) {
            telnetSendBytes(s, len);
            (void)fwrite(s, 1, len, stdout);
        }

        lua_pop(L, 1); // pop the string from luaL_tolstring
    }

    telnetSendBytes("\r\n", 2);
    putchar('\n');
    return 0;
}

// sleep(seconds)
static int luaNpluaSleep(lua_State *L) {
    double secs = luaL_checknumber(L, 1);
    if (!isfinite(secs)) {
        return luaL_argerror(L, 1, "finite number expected");
    }
    if (secs < 0) {
        secs = 0;
    }

    // Convert to ms, clamp to 32-bit
    double msDouble = secs * 1000.0;
    if (msDouble < 0) {
        msDouble = 0;
    }
    if (msDouble > UINT32_MAX) {
        msDouble = UINT32_MAX;
    }

    uint32_t ms = (uint32_t)(msDouble + 0.5);
    sleep_ms(ms);
    return 0;
}

// nplua.led(on)
static int luaNpluaLed(lua_State *L) {
    int on = lua_toboolean(L, 1) ? 1 : 0;
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on);
    return 0;
}

static int luaOsClock(lua_State *L) {
    // seconds since boot as a Lua number
    absolute_time_t now = get_absolute_time();
    double secs = to_us_since_boot(now) / 1000000.0;
    lua_pushnumber(L, secs);
    return 1;
}

static int luaOsUnsupported(lua_State *L) {
    const char *name = lua_tostring(L, lua_upvalueindex(1));
    if (!name) name = "this os.* function";
    lua_pushfstring(L, "%s is not supported on NPLua", name);
    return lua_error(L);
}

void luaInit(void) {
    if (luaState) return;

    luaState = lua_newstate(luaAllocator, &luaAllocatorState);
    if (!luaState) {
        printf("luaInit: failed to create lua_State\n");
        return;
    }
    lua_atpanic(luaState, luaPanic);

    // Open only the core libs we actually want
    luaL_requiref(luaState, "_G", luaopen_base, 1);          lua_pop(luaState, 1);
    luaL_requiref(luaState, LUA_TABLIBNAME,  luaopen_table, 1);  lua_pop(luaState, 1);
    luaL_requiref(luaState, LUA_STRLIBNAME,  luaopen_string, 1); lua_pop(luaState, 1);
    luaL_requiref(luaState, LUA_MATHLIBNAME, luaopen_math, 1);   lua_pop(luaState, 1);
    luaL_requiref(luaState, LUA_UTF8LIBNAME, luaopen_utf8, 1);   lua_pop(luaState, 1);
    luaL_requiref(luaState, LUA_DBLIBNAME, luaopen_debug, 1); lua_pop(luaState, 1);

    // Build a tiny 'os' table
    lua_newtable(luaState);

    // os.clock -> our Pico-based clock
    lua_pushcfunction(luaState, luaOsClock);
    lua_setfield(luaState, -2, "clock");

    // Stub out unsupported stuff so code fails loudly instead of silently
    const char *unsupported[] = {
        "execute", "remove", "rename", "tmpname",
        "getenv", "setlocale", "exit",
        NULL
    };

    for (int i = 0; unsupported[i]; i++) {
        lua_pushstring(luaState, unsupported[i]);
        lua_pushcclosure(luaState, luaOsUnsupported, 1);
        lua_setfield(luaState, -2, unsupported[i]);
    }

    lua_setglobal(luaState, "os");

    // Override global print
    lua_pushcfunction(luaState, luaNpluaPrint);
    lua_setglobal(luaState, "print");
    
    // Register gpio table
    npluaRegisterGpio(luaState);
    
    lua_pushcfunction(luaState, luaNpluaSleep);
    lua_setglobal(luaState, "sleep");
    
    lua_pushcfunction(luaState, luaNpluaLed);
    lua_setglobal(luaState, "led");

    luaOutputLine("Lua VM initialized.");
}

void luaRunChunk(const char *code, size_t length) {
    if (!luaState) {
        luaInit();
        if (!luaState) {
            luaOutputLine("Lua error: VM not available.");
            return;
        }
    }

    if (!code || length == 0) {
        luaOutputLine("Lua: empty chunk, nothing to run.");
        return;
    }

    // Debug: show that we actually got something
    char dbg[80];
    snprintf(dbg, sizeof(dbg), "[NPLua] running Lua chunk (%u bytes)", (unsigned)length);
    luaOutputLine(dbg);

    // Try to load the chunk
    int status = luaL_loadbuffer(luaState, code, length, "telnet_chunk");
    if (status != LUA_OK) {
        const char *err = lua_tostring(luaState, -1);
        luaOutputLine(err ? err : "Unknown Lua load error");
        lua_pop(luaState, 1); // pop error
        lua_gc(luaState, LUA_GCCOLLECT, 0);
        return;
    }

    // Call the chunk
    status = lua_pcall(luaState, 0, 0, 0);
    if (status != LUA_OK) {
        const char *err = lua_tostring(luaState, -1);
        luaOutputLine(err ? err : "Unknown Lua runtime error");
        lua_pop(luaState, 1);
        lua_gc(luaState, LUA_GCCOLLECT, 0);
        return;
    }

    lua_gc(luaState, LUA_GCCOLLECT, 0);
}
