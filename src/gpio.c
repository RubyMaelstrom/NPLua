#include "gpio.h"

#include "pico/stdlib.h"
#include "lua.h"
#include "lauxlib.h"

static bool isUserGpio(lua_Integer pin) {
    // GPIOs 23-25 and 29 have board-level functions on Pico 2 W. The pins
    // available on the headers are 0-22 and 26-28.
    return (pin >= 0 && pin <= 22) || (pin >= 26 && pin <= 28);
}

static int checkUserGpio(lua_State *L, int argument) {
    lua_Integer pin = luaL_checkinteger(L, argument);
    if (!isUserGpio(pin)) {
        return luaL_error(L, "invalid GPIO pin: " LUA_INTEGER_FMT, pin);
    }
    return (int)pin;
}

// gpio.setMode(pin, dir)
static int luaGpioSetMode(lua_State *L) {
    int pin = checkUserGpio(L, 1);
    lua_Integer dir = luaL_checkinteger(L, 2); // gpio.INPUT or gpio.OUTPUT

    if (dir != 0 && dir != 1) {
        return luaL_argerror(L, 2, "gpio.INPUT or gpio.OUTPUT expected");
    }

    gpio_init(pin);
    gpio_set_dir(pin, dir == 0 ? GPIO_IN : GPIO_OUT);

    return 0;
}

// gpio.write(pin, value)
static int luaGpioWrite(lua_State *L) {
    int pin   = checkUserGpio(L, 1);
    int value = lua_toboolean(L, 2) ? 1 : 0;

    gpio_put(pin, value);
    return 0;
}

// gpio.read(pin) -> boolean
static int luaGpioRead(lua_State *L) {
    int pin = checkUserGpio(L, 1);

    int value = gpio_get(pin);
    lua_pushboolean(L, value ? 1 : 0);
    return 1;
}

// gpio.toggle(pin)
static int luaGpioToggle(lua_State *L) {
    int pin = checkUserGpio(L, 1);

    bool value = gpio_get(pin);
    gpio_put(pin, !value);
    return 0;
}

void npluaRegisterGpio(lua_State *L) {
    // Create gpio table
    lua_newtable(L);

    // Methods
    lua_pushcfunction(L, luaGpioSetMode);
    lua_setfield(L, -2, "setMode");

    lua_pushcfunction(L, luaGpioWrite);
    lua_setfield(L, -2, "write");

    lua_pushcfunction(L, luaGpioRead);
    lua_setfield(L, -2, "read");

    lua_pushcfunction(L, luaGpioToggle);
    lua_setfield(L, -2, "toggle");

    // Constants
    lua_pushinteger(L, 0);
    lua_setfield(L, -2, "INPUT");

    lua_pushinteger(L, 1);
    lua_setfield(L, -2, "OUTPUT");

    // Set global gpio = { ... }
    lua_setglobal(L, "gpio");
}
