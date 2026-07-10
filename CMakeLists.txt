cmake_minimum_required(VERSION 3.13...3.27)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE MinSizeRel CACHE STRING
        "Build type (MinSizeRel is recommended for the Pico)" FORCE)
endif()

set(NPLUA_PICO_SDK_VERSION "2.3.0" CACHE STRING
    "Pico SDK version used when the SDK is fetched automatically")

# Select the target before importing the SDK/project toolchain. Callers can
# still override this with -DPICO_BOARD=... on the CMake command line.
if (NOT DEFINED PICO_BOARD)
    set(PICO_BOARD pico2_w CACHE STRING "Pico SDK board target")
endif()

# Prefer an installed SDK, but make a clean checkout self-bootstrapping.
if (NOT PICO_SDK_PATH AND NOT DEFINED ENV{PICO_SDK_PATH} AND
    NOT DEFINED PICO_SDK_FETCH_FROM_GIT AND
    NOT DEFINED ENV{PICO_SDK_FETCH_FROM_GIT})
    set(PICO_SDK_FETCH_FROM_GIT ON)
endif()
if (NOT PICO_SDK_FETCH_FROM_GIT_TAG AND
    NOT DEFINED ENV{PICO_SDK_FETCH_FROM_GIT_TAG})
    set(PICO_SDK_FETCH_FROM_GIT_TAG "${NPLUA_PICO_SDK_VERSION}" CACHE STRING
        "Pico SDK Git tag" FORCE)
endif()

include(pico_sdk_import.cmake)

project(NPLua VERSION 0.2.0 LANGUAGES C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

pico_sdk_init()

if (PICO_SDK_VERSION_STRING VERSION_LESS "${NPLUA_PICO_SDK_VERSION}")
    message(FATAL_ERROR
        "NPLua requires Pico SDK ${NPLUA_PICO_SDK_VERSION} or newer; "
        "found ${PICO_SDK_VERSION_STRING}")
endif()

set(NPLUA_WIFI_SSID "YourWifiSSID" CACHE STRING "Wi-Fi network name")
set(NPLUA_WIFI_PASSWORD "YourWifiPassword" CACHE STRING "Wi-Fi password")
set(NPLUA_WIFI_COUNTRY "CYW43_COUNTRY_GERMANY" CACHE STRING
    "CYW43 country-code constant")
set(NPLUA_WIFI_AUTH "CYW43_AUTH_WPA2_MIXED_PSK" CACHE STRING
    "CYW43 Wi-Fi authentication constant")
set(NPLUA_WIFI_CONNECT_TIMEOUT_MS 30000 CACHE STRING
    "Initial and reconnect Wi-Fi timeout in milliseconds")
set(NPLUA_WIFI_RETRY_DELAY_MS 5000 CACHE STRING
    "Delay between Wi-Fi connection attempts in milliseconds")
set(NPLUA_WIFI_HEALTH_INTERVAL_MS 5000 CACHE STRING
    "Interval between Wi-Fi link checks in milliseconds")
set(NPLUA_TELNET_PORT 23 CACHE STRING "TCP port for the NPLua console")
set(NPLUA_TELNET_LINE_SIZE 512 CACHE STRING
    "Maximum Telnet input line size including its terminator")
set(NPLUA_TELNET_TX_BUFFER_SIZE 2048 CACHE STRING
    "Telnet transmit queue size in bytes")
set(NPLUA_CORE1_STACK_SIZE 8192 CACHE STRING "Lua core stack size in bytes")
set(NPLUA_LUA_HEAP_LIMIT 262144 CACHE STRING "Maximum Lua heap usage in bytes")

set(NPLUA_USER_CONFIG "${CMAKE_CURRENT_LIST_DIR}/nplua_user.cmake" CACHE FILEPATH
    "Optional local NPLua configuration file")
if (EXISTS "${NPLUA_USER_CONFIG}")
    include("${NPLUA_USER_CONFIG}")
endif()

configure_file(
    src/nplua_config.h.in
    generated/nplua_config.h
    @ONLY ESCAPE_QUOTES
)

add_subdirectory(lua)

set(NPLUA_SOURCES
    src/main.c
    src/console.c
    src/luaEmbed.c
    src/telnet.c
    src/gpio.c
    src/core.c
)

add_executable(nplua ${NPLUA_SOURCES})

target_include_directories(nplua PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/lua
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_compile_definitions(nplua PRIVATE
    CYW43_HOST_NAME="pico-nplua"
    # The default scratch-X stack region is smaller than Lua needs. NPLua
    # supplies an explicitly sized core-1 stack from normal SRAM instead.
    PICO_CORE1_STACK_SIZE=0
)

target_link_libraries(nplua
    pico_stdlib
    pico_cyw43_arch_lwip_threadsafe_background
    pico_multicore
    lua
    hardware_watchdog
)

if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(${NPLUA_SOURCES} PROPERTIES
        COMPILE_OPTIONS "-Wall;-Wextra;-Wformat=2;-Wshadow"
    )
endif()

pico_set_program_name(nplua "NPLua")
pico_set_program_version(nplua "${PROJECT_VERSION}")

pico_enable_stdio_usb(nplua 1)
pico_enable_stdio_uart(nplua 0)

pico_add_extra_outputs(nplua)
