cmake_minimum_required(VERSION 3.13)

# Import the Pico SDK (assumes pico_sdk_import.cmake is in this directory)
include(pico_sdk_import.cmake)

# Name of the project + languages
project(NPLua C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Make sure we target Pico 2 W
# You can also pass this via the command line: -DPICO_BOARD=pico2_w
set(PICO_BOARD pico2_w)

# Initialize the Pico SDK
pico_sdk_init()

add_subdirectory(lua)

# Our executable
add_executable(nplua
    src/main.c
    src/console.c
    src/luaEmbed.c
    src/telnet.c
    src/gpio.c
    src/core.c
)

target_include_directories(nplua PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/lua
)

# (Optional) Compile-time defs (your SSID/pass/etc.)
target_compile_definitions(nplua PRIVATE
    WIFI_SSID="SSID"
    WIFI_PASSWORD="PASS"
    CYW43_HOST_NAME="pico-nplua"
)

# Link libraries:
# - pico_stdlib for stdio, timing, etc.
# - pico_cyw43_arch_lwip_threadsafe_background for WiFi/lwIP later
target_link_libraries(nplua
    pico_stdlib
    pico_cyw43_arch_lwip_threadsafe_background
    pico_multicore
    lua
    hardware_watchdog
)

# Enable USB stdio, disable UART (you can flip these if you want)
pico_enable_stdio_usb(nplua 1)
pico_enable_stdio_uart(nplua 0)

# Create UF2, bin, etc.
pico_add_extra_outputs(nplua)
