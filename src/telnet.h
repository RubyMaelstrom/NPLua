// telnet.h
#pragma once

#include <stddef.h>

// Initialize the telnet listener (raw TCP API)
void telnetInit(void);

// Called by console to write text to the active telnet client
void telnetSend(const char *text);

// Send a byte sequence; useful for Lua strings containing embedded NULs.
void telnetSendBytes(const void *data, size_t length);

// Send one text line, adding the Telnet CRLF sequence atomically.
void telnetSendLine(const char *text);

// Called by console to request that the active session be closed.
void telnetCloseActive(void);

// Process deferred connection work from the main loop.
void telnetProcess(void);
