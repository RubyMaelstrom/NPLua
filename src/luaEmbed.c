// luaEmbed.c
#include "luaEmbed.h"
#include "gpio.h"

#include <stdio.h>
#include <string.h>

//#include "telnet.h"
#include "core.h"
#include "pico/time.h"
#include "pico/cyw43_arch.h"

// Lua headers – you need these in your include path
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

static lua_State *luaState = NULL;

// Small helper: send output to telnet (and also to USB for debug)
static void luaOutputLine(const char *text) {
    if (!text) text = "";

    // Send to core0 via the IPC buffer
    npluaEnqueueOutput(text);

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

    char buffer[256];
    buffer[0] = '\0';
    size_t bufLen = 0;

    for (int i = 1; i <= top; i++) {
        size_t len;
        const char *s = luaL_tolstring(L, i, &len); // converts to string, leaves result on stack

        if (!s) {
            s = "";
            len = 0;
        }

        // Add a space between arguments
        if (i > 1 && bufLen < sizeof(buffer) - 1) {
            buffer[bufLen++] = ' ';
            buffer[bufLen] = '\0';
        }

        // Append s in chunks if needed
        size_t remaining = sizeof(buffer) - 1 - bufLen;
        if (len > remaining) {
            len = remaining;
        }
        memcpy(&buffer[bufLen], s, len);
        bufLen += len;
        buffer[bufLen] = '\0';

        lua_pop(L, 1); // pop the string from luaL_tolstring
        if (bufLen >= sizeof(buffer) - 1) {
            break;
        }
    }

    luaOutputLine(buffer);
    return 0;
}

// sleep(seconds)
static int luaNpluaSleep(lua_State *L) {
    double secs = luaL_checknumber(L, 1);
    if (secs < 0) {
        secs = 0;
    }

    // Convert to ms, clamp to 32-bit
    double msDouble = secs * 1000.0;
    if (msDouble < 0) {
        msDouble = 0;
    }
    if (msDouble > 4294967295.0) {
        msDouble = 4294967295.0;
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
    double secs = to_ms_since_boot(now) / 1000.0;
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

    luaState = luaL_newstate();
    if (!luaState) {
        printf("luaInit: failed to create lua_State\n");
        return;
    }

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
        return;
    }

    // Call the chunk
    status = lua_pcall(luaState, 0, LUA_MULTRET, 0);
    if (status != LUA_OK) {
        const char *err = lua_tostring(luaState, -1);
        luaOutputLine(err ? err : "Unknown Lua runtime error");
        lua_pop(luaState, 1);
        return;
    }

    // If we reach here, code ran without error. Any print() calls should have
    // gone through luaNpluaPrint -> telnetSend already.
}