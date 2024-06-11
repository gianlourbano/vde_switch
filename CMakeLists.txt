cmake_minimum_required(VERSION 3.13)
project("vde_switch"
    DESCRIPTION "just fuckin around and finding out"
    VERSION 0.0.1
)

set(CMAKE_C_STANDARD 99)

include_directories(includes src)


add_subdirectory(mods)

file(GLOB_RECURSE SOURCES "src/*.c")

add_executable(vde_switch main.c ${SOURCES})

target_include_directories(vde_switch PUBLIC
                           "${PROJECT_BINARY_DIR}")

