set(LIB
    PUBLIC eigen
    util
)

set(SRC
  PUBLIC vector.h
  PUBLIC color.h
  PUBLIC lut.h
  PUBLIC geom.h 
  PUBLIC quat.h
  PUBLIC matrix.h
  PUBLIC bspline.h
  PUBLIC mix.h
  PUBLIC wideint.h
  PRIVATE tri_aabb_isect.h
  color.cc
)

lt_add_library(math "${SRC}" "${LIB}" STATIC)

# Global (PUBLIC) so every consumer of math/wideint.h sees the same choice.
if (LITESTL_NO_INT128)
  target_compile_definitions(math PUBLIC LITESTL_NO_INT128)
endif()

