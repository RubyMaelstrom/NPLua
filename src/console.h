// console.h
#pragma once

#include <stddef.h>

typedef void (*ConsoleWriteFn)(const char *text);
typedef void (*ConsolePrintfFn)(const char *fmt, ...);

// Called once at startup
void consoleInit(ConsoleWriteFn writeFn, ConsolePrintfFn printfFn);

// Called whenever a full line of input is received (no trailing \r\n)
void consoleHandleLine(const char *line);

// Optional, if you want a prompt after each command
const char *consoleGetPrompt(void);