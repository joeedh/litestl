# Vendored lz4 (BSD-2-Clause), v1.10.0 — block API only (lz4.c + lz4hc.c).
# Built as plain C so it compiles cleanly for native and WASM without the
# C++20 flags the litestl macros stamp onto C++ targets.
add_library(lz4 STATIC lz4.c lz4hc.c)
target_include_directories(lz4 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
