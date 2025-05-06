set(LIB
    util
)

set(SRC
  PUBLIC path.h
  path.cc
)

lt_add_library(litestl_path "${SRC}" "${LIB}" STATIC)


