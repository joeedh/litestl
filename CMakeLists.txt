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

# clang-cl (and clang targeting MSVC) default to delayed template parsing,
# which accepts code that other compilers reject. Keep every clang strict.
add_compile_options($<$<COMPILE_LANG_AND_ID:CXX,Clang>:-fno-delayed-template-parsing>)

include("build_files/macros.cmake")

# N-API runtime: needs the node-api headers (`node configureEnv.mjs --fetch-deps`).
if (DEFINED ENV{LITESTL_DEPS_DIR})
  set(_lt_napi_default "$ENV{LITESTL_DEPS_DIR}/node-api-headers")
else()
  set(_lt_napi_default "${CMAKE_CURRENT_SOURCE_DIR}/build/deps/node-api-headers")
endif()
set(LITESTL_NODE_API_HEADERS "${_lt_napi_default}" CACHE PATH "node-api-headers package directory")
set(LITESTL_WITH_NAPI OFF)
if (NOT BUILD_WASM AND EXISTS "${LITESTL_NODE_API_HEADERS}/include/node_api.h")
  set(LITESTL_WITH_NAPI ON)
  # The static libraries get linked into .node shared modules.
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
else()
  message(STATUS "litestl: node-api headers not found, skipping the N-API runtime")
endif()

add_subdirectory(platform)
add_subdirectory(util)
add_subdirectory(math)
add_subdirectory(extern)
add_subdirectory(binding)

if (LITESTL_WITH_NAPI)
  add_subdirectory(binding/napi)
endif()
add_subdirectory(path)
if (LITESTL_BUILD_TESTS)
  add_subdirectory(tests/bindings)
  # The wasi toolchain builds only the conformance fixture; Emscripten builds
  # still get tests/ for the test_binding_system harness.
  if (NOT LITESTL_WASM_CLANG_TOOLCHAIN)
    add_subdirectory(tests)
  endif()
endif()
add_subdirectory(io)
add_subdirectory(attr)

# build a target that's just litestl (Emscripten builds only)
if (BUILD_WASM AND NOT LITESTL_WASM_CLANG_TOOLCHAIN)
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
