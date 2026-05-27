set(LIB
	util
	math
  binding
  lz4
)

set(SRC
  PUBLIC serial.h
  PUBLIC binfile.h
  PUBLIC compress.h
  serial.cc
  compress.cc
)

lt_add_library(io "${SRC}" "${LIB}" STATIC)

