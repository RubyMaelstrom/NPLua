// telnet.h
#pragma once

// Initialize the telnet listener (raw TCP API)
void telnetInit(void);

// Called by console to write text to the active telnet client
void telnetSend(const char *text);

//Called by console to close the active telnet session
void telnetCloseActive(void);