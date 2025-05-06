set(LIB
    eigen
    util
)

set(SRC
  PUBLIC binding.h
  PUBLIC binding_base.h
  PUBLIC binding_types.h
  PUBLIC binding_utils.h
  binding.cc
)

lt_add_library(binding "${SRC}" "${LIB}" STATIC)
add_subdirectory(generators)

