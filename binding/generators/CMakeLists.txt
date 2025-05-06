set(LIB
    eigen
    util
    binding
)

set(SRC
  PUBLIC typescript.h
  typescript.cc
)

lt_add_library(binding_generators "${SRC}" "${LIB}" STATIC)

