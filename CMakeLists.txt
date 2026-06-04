cmake_minimum_required(VERSION 3.12)
project(pico_serial_proxy C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(pico_serial_proxy STATIC
  src/pico_serial_proxy.c
)

target_include_directories(pico_serial_proxy PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(pico_serial_proxy PRIVATE _GNU_SOURCE _XOPEN_SOURCE=700)
