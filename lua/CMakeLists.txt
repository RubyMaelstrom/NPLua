# lua/CMakeLists.txt
cmake_minimum_required(VERSION 3.13)

add_library(lua STATIC
    # Add your Lua source files here (from official lua tarball)
    # For example (Lua 5.4.x):
    lapi.c lcode.c lctype.c ldebug.c ldo.c ldump.c lfunc.c lgc.c
    llex.c lmem.c lobject.c lopcodes.c lparser.c lstate.c lstring.c
    ltable.c ltm.c lundump.c lvm.c lzio.c

    lauxlib.c lbaselib.c lcorolib.c ldblib.c liolib.c
    lmathlib.c loslib.c lstrlib.c ltablib.c lutf8lib.c loadlib.c
    linit.c
)

target_include_directories(lua PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)