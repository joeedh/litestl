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
  PUBLIC predicates.h
  PRIVATE tri_aabb_isect.h
  color.cc
)

lt_add_library(math "${SRC}" "${LIB}" STATIC)

# Global (PUBLIC) so every consumer of math/wideint.h sees the same choice.
if (LITESTL_NO_INT128)
  target_compile_definitions(math PUBLIC LITESTL_NO_INT128)
endif()


# Robust geometric predicates. A separate library so the strict IEEE flags
# apply to exactly these translation units. predicates_stats is the same code
# with per-stage hit counters compiled in (for tests); link one or the other.
set(PRED_SRC
  predicates/api.cc
  predicates/expansion.cc
  predicates/orient.cc
  predicates/incircle.cc
  predicates/insphere.cc
)
foreach(pred_target predicates predicates_stats)
  add_library(${pred_target} STATIC ${PRED_SRC})
  target_link_libraries(${pred_target} PUBLIC math util)
  target_include_directories(${pred_target} PUBLIC .. ../..)
  if (CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(${pred_target} PRIVATE /fp:precise)
  else()
    target_compile_options(${pred_target} PRIVATE
      -ffp-contract=off -fno-fast-math -fno-associative-math
      -fno-unsafe-math-optimizations -fno-reciprocal-math)
  endif()
endforeach()
target_compile_definitions(predicates_stats PRIVATE LITESTL_PRED_STATS)

# Deterministic elementary functions (detmath.h): strict IEEE flags, so the
# results do not depend on the consumer's flags or on the platform libm.
add_library(detmath STATIC detmath.cc detmath.h)
target_include_directories(detmath PUBLIC .. ../..)
if (CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  target_compile_options(detmath PRIVATE /fp:precise)
else()
  target_compile_options(detmath PRIVATE
    -ffp-contract=off -fno-fast-math -fno-associative-math
    -fno-unsafe-math-optimizations -fno-reciprocal-math)
endif()
