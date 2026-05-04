set(LIB
	util
	math
  binding
)

set(SRC
  PUBLIC serial.h
  PUBLIC binfile.h
  serial.cc
)

lt_add_library(io "${SRC}" "${LIB}" STATIC)

