cmake_minimum_required(VERSION 3.15)
project(LiteStl C CXX)

if (NOT BUILD_WASM) 
	include(CTest)
endif()

set(CMAKE_CXX_STANDARD_REQUIRED true)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_SOURCE_FILE_EXTENSIONS .cc)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile commands")


option(BUILD_WASM "Build wasm library" OFF)
option(LITESTL_BUILD_TESTS "Build litestl's own tests" ON)
option(LITESTL_NO_INT128 "Force the portable (non-__int128) wide-integer path" OFF)
set(LITESTL_TEST_SCALE 1 CACHE STRING
    "Default multiplier for randomized test case counts (env LITESTL_TEST_SCALE overrides)")
set(EMSDK_PATH "" CACHE PATH "Path to emsdk")
set(EMCC_PATH "" CACHE PATH "Path to emcc")

enable_testing()

set(CMAKE_OPTIMIZE_DEPENDENCIES true)

include("build_files/macros.cmake")

add_subdirectory(platform)
add_subdirectory(util)
add_subdirectory(math)
add_subdirectory(extern)
add_subdirectory(binding)
add_subdirectory(path)
if (LITESTL_BUILD_TESTS)
  add_subdirectory(tests)
endif()
add_subdirectory(io)

# build a target that's just litestl
if (BUILD_WASM)
    set(LITESTL_LIB 
        platform
        util
        math
        binding
        binding_generators
    )

    get_property(LtWasmSymbols GLOBAL PROPERTY LT_WASM_SYMBOLS)
    build_wasm(litestl "" "${LITESTL_LIB}" "${LtWasmSymbols}")
endif()
