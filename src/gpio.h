#pragma once
#include "lua.h"

// Register the 'gpio' table into the given Lua state
void npluaRegisterGpio(lua_State *L);