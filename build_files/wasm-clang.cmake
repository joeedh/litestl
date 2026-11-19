# WASM toolchain using a stock clang + the wasi sysroot, no Emscripten.
#
# The sysroot and compiler-rt builtins come from `node configureEnv.mjs
# --fetch-deps` (LITESTL_DEPS_DIR, default <repo>/build/deps). The compiler is
# VS's bundled clang on Windows and the system clang elsewhere; override with
# -DLITESTL_WASM_CLANG=<path to clang++>.
#
# Targets wasm32-wasip1-threads: litestl's allocator and task system use
# std::mutex/std::thread, which the non-threaded wasi libc++ lacks. Modules
# import a shared memory and never spawn threads unless asked to.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR wasm32)

set(BUILD_WASM ON CACHE BOOL "" FORCE)
set(LITESTL_WASM_CLANG_TOOLCHAIN ON CACHE BOOL "" FORCE)

if (DEFINED ENV{LITESTL_DEPS_DIR})
  set(_lt_deps "$ENV{LITESTL_DEPS_DIR}")
else()
  get_filename_component(_lt_deps "${CMAKE_CURRENT_LIST_DIR}/../build/deps" ABSOLUTE)
endif()
file(TO_CMAKE_PATH "${_lt_deps}" _lt_deps)
set(LITESTL_WASI_SYSROOT "${_lt_deps}/wasi-sysroot" CACHE PATH "wasi sysroot")
set(LITESTL_WASI_BUILTINS "${_lt_deps}/wasi-builtins/libclang_rt.builtins-wasm32.a"
    CACHE FILEPATH "compiler-rt builtins for wasm32")

if (NOT LITESTL_WASM_CLANG)
  if (CMAKE_HOST_WIN32 AND DEFINED ENV{VCINSTALLDIR})
    file(TO_CMAKE_PATH "$ENV{VCINSTALLDIR}" _lt_vc)
    set(_lt_hint "${_lt_vc}/Tools/Llvm/x64/bin")
  endif()
  find_program(LITESTL_WASM_CLANG NAMES clang++ clang++-20 clang++-19 clang++-18
               HINTS ${_lt_hint} REQUIRED)
endif()
get_filename_component(_lt_clang_dir "${LITESTL_WASM_CLANG}" DIRECTORY)
set(CMAKE_CXX_COMPILER "${LITESTL_WASM_CLANG}")
if (EXISTS "${_lt_clang_dir}/clang${CMAKE_HOST_EXECUTABLE_SUFFIX}")
  set(CMAKE_C_COMPILER "${_lt_clang_dir}/clang${CMAKE_HOST_EXECUTABLE_SUFFIX}")
else()
  string(REPLACE "clang++" "clang" _lt_cc "${LITESTL_WASM_CLANG}")
  set(CMAKE_C_COMPILER "${_lt_cc}")
endif()

set(_lt_triple wasm32-wasip1-threads)
set(CMAKE_C_COMPILER_TARGET ${_lt_triple})
set(CMAKE_CXX_COMPILER_TARGET ${_lt_triple})
set(CMAKE_SYSROOT "${LITESTL_WASI_SYSROOT}")

# The wasi crt/libs are static; skip CMake's try-run of a full executable.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# WASM selects litestl's wasm32 paths (e.g. util::Vector's 8-byte padding),
# matching the Emscripten build.
set(_lt_flags "-pthread -fno-exceptions -Wno-invalid-offsetof -DWASM -ffp-contract=off -fno-delayed-template-parsing")
set(CMAKE_C_FLAGS_INIT "${_lt_flags}")
set(CMAKE_CXX_FLAGS_INIT "${_lt_flags}")

# VS's clang has no wasm32 compiler-rt, so the default runtime libraries are
# named explicitly. Modules are reactors (no main) importing a shared memory.
# wasm-ld's default 64KB stack is too small for the TS generator.
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "-pthread -mexec-model=reactor -nodefaultlibs -Wl,-z,stack-size=8388608 \
-Wl,--import-memory,--export-memory,--shared-memory,--max-memory=1073741824")
set(CMAKE_CXX_STANDARD_LIBRARIES "-lc++ -lc++abi -lc \"${LITESTL_WASI_BUILTINS}\"")
set(CMAKE_C_STANDARD_LIBRARIES "-lc \"${LITESTL_WASI_BUILTINS}\"")
