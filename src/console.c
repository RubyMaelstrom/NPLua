// console.c
#include "console.h"
#include "telnet.h"
#include "hardware/watchdog.h"
#include "core.h"

#include <string.h>

#define LUA_BUF_SIZE (NPLUA_MAX_CHUNK_SIZE + 1u)

typedef enum {
    ConsoleModeCommand,
    ConsoleModeLuaUpload
} ConsoleMode;

static volatile ConsoleMode consoleMode = ConsoleModeCommand;
static char luaBuf[LUA_BUF_SIZE];
static size_t luaLen = 0;

static ConsoleWriteFn consoleWrite = NULL;
static ConsolePrintfFn consolePrintf = NULL;

void consoleInit(ConsoleWriteFn writeFn, ConsolePrintfFn printfFn) {
    consoleWrite = writeFn;
    consolePrintf = printfFn;
    consoleMode = ConsoleModeCommand;
    luaLen = 0;

    if (consoleWrite) {
        consoleWrite("NPLua ready. Type 'lua' to enter Lua mode, 'help' for help.\r\n");
        const char *prompt = consoleGetPrompt();
        if (prompt) {
            consoleWrite(prompt);
        }
    }
}

const char *consoleGetPrompt(void) {
    if (consoleMode != ConsoleModeCommand) {
        return NULL;
    }

    if (npluaIsLuaRunning()) {
        // Lua chunk is currently executing on core1; don't show prompt yet
        return NULL;
    }

    return "NPLua> ";
}

static void startLuaUpload(void) {
    if (npluaIsLuaRunning()) {
        consoleWrite("Lua is already running; wait for it to finish.\r\n");
        return;
    }

    consoleMode = ConsoleModeLuaUpload;
    luaLen = 0;
    consoleWrite("Entering Lua upload mode. End with :done on its own line.\r\n");
}

static void abortLuaUpload(const char *reason) {
    consoleMode = ConsoleModeCommand;
    luaLen = 0;
    if (reason && consolePrintf) {
        consolePrintf("Lua upload aborted: %s\r\n", reason);
    }
}

static void appendLuaLine(const char *line) {
    size_t lineLen = strlen(line);
    // +1 for '\n'
    if (luaLen + lineLen + 1 >= LUA_BUF_SIZE) {
        abortLuaUpload("buffer full");
        return;
    }

    memcpy(&luaBuf[luaLen], line, lineLen);
    luaLen += lineLen;
    luaBuf[luaLen++] = '\n';
}

static void finishLuaUpload(void) {
    // Null-terminate for convenience
    if (luaLen >= LUA_BUF_SIZE) {
        abortLuaUpload("internal overflow");
        return;
    }

    luaBuf[luaLen] = '\0';

    if (luaLen == 0) {
        consoleWrite("Lua chunk is empty; nothing to run.\r\n");
        consoleMode = ConsoleModeCommand;
        return;
    }

    if (consolePrintf) {
        consolePrintf("Running Lua chunk (%u bytes)...\r\n", (unsigned)luaLen);
    }

    if (!npluaQueueChunk(luaBuf, luaLen)) {
        consoleWrite("Unable to queue Lua chunk; another job is active.\r\n");
    }

    consoleMode = ConsoleModeCommand;
    luaLen = 0;
}

static void handleCommandLine(const char *line) {
    // Accept both with and without leading colon, for convenience
    if (strcmp(line, "lua") == 0 || strcmp(line, ":lua") == 0) {
        startLuaUpload();
        return;
    }

    if (strcmp(line, "help") == 0 || strcmp(line, ":help") == 0) {
        consoleWrite("Commands:\r\n");
        consoleWrite("  lua    - enter multi-line Lua upload mode, end with :done\r\n");
        consoleWrite("  help   - show this help\r\n");
        consoleWrite("  quit   - close the telnet connection\r\n");
        consoleWrite("  reboot - restart the Pico\r\n");
        return;
    }

    if (strcmp(line, "quit") == 0 || strcmp(line, ":quit") == 0) {
        consoleWrite("Goodbye!\r\n");
        // Drop the telnet connection; client will see socket close
        telnetCloseActive();
        return;
    }
    
    if (strcmp(line, "reboot") == 0 || strcmp(line, ":reboot") == 0) {
        consoleWrite("Rebooting...\r\n");
        telnetCloseActive();
        watchdog_reboot(0, 0, 200);
        return;
    }

    if (consolePrintf) {
        consolePrintf("Unknown command: %s\r\n", line);
    }
}

void consoleHandleLine(const char *line) {
    if (consoleMode == ConsoleModeCommand) {
        handleCommandLine(line);
    } else if (consoleMode == ConsoleModeLuaUpload) {
        if (strcmp(line, ":done") == 0) {
            finishLuaUpload();
        } else {
            appendLuaLine(line);
        }
    }
}
