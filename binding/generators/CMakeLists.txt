set(LIB
    eigen
    util
    binding
    litestl_path
)

set(SRC
  PUBLIC typescript.h
  typescript.cc
)

lt_add_library(binding_generators "${SRC}" "${LIB}" STATIC)

