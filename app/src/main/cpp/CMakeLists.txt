cmake_minimum_required(VERSION 3.22.1)
project(c2core)

add_library(c2core SHARED
    c2_core.cpp
    string_xor.S
    anti_debug.S
)

target_compile_options(c2core PRIVATE -O2 -fvisibility=hidden)
target_link_libraries(c2core log)