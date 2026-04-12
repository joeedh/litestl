cmake_minimum_required(VERSION 3.15)
project(LiteStl C CXX)

set(CMAKE_CXX_STANDARD_REQUIRED true)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_SOURCE_FILE_EXTENSIONS .cc)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile commands")

option(BUILD_WASM "Build wasm library" OFF)
set(EMSDK_PATH "" CACHE PATH "Path to emsdk")
set(EMCC_PATH "" CACHE PATH "Path to emcc")

message("BUILD_WASM" ${BUILD_WASM})

enable_testing()

set(CMAKE_OPTIMIZE_DEPENDENCIES true)

include("build_files/macros.cmake")

add_subdirectory(platform)
add_subdirectory(util)
add_subdirectory(math)
add_subdirectory(extern)
add_subdirectory(tests)
add_subdirectory(binding)
add_subdirectory(path)
