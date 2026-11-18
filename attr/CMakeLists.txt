# litestl::attr — paged copy-on-write attribute columns (documentation/attributes.md).
# FP flags are PUBLIC: mix kernels are header templates instantiated in
# consumer translation units as well as here, and must not be contracted.
add_library(attr STATIC
  registry.cc
  group.cc
  pool_store.cc
)
target_link_libraries(attr PUBLIC util math)
target_include_directories(attr PUBLIC .. ../..)
if (CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  target_compile_options(attr PUBLIC /fp:precise)
else()
  target_compile_options(attr PUBLIC
    -ffp-contract=off -fno-fast-math -fno-associative-math
    -fno-unsafe-math-optimizations -fno-reciprocal-math)
endif()
