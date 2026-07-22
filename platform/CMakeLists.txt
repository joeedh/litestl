set(LIB
)

set(SRC
  PUBLIC export.h
  PUBLIC time.h
  PUBLIC cpu.h
  PUBLIC platform.h
  common.cc
)

if (NOT BUILD_WASM AND CMAKE_SYSTEM_NAME STREQUAL "Windows")
set(SRC
    ${SRC}
    win32.cc
    debug_win32.cc
)
endif()

# linux.cc is generic POSIX (std::chrono/std::thread), so it covers macOS too.
if (NOT BUILD_WASM AND (CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin"))
set(SRC
    ${SRC}
    linux.cc
)
endif()

if (BUILD_WASM)
    set(SRC
        ${SRC}
        wasm.cc
    )
endif()

lt_add_library(platform "${SRC}" "${LIB}" OBJECT)
