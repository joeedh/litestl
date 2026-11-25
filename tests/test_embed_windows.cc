/* Embedding check (Windows only): every public header must compile after <windows.h>
 * without NOMINMAX, and next to Unreal-style check/verify/ensure macros. Excluded:
 * util/wasm.h (WASM only), and math/lut.h and math/mix.h (unused and do not compile on
 * their own). */
#include <windows.h>
#define check(expr) ((void)(expr))
#define verify(expr) ((void)(expr))
#define ensure(expr) (!!(expr))

#include "litestl/util/alloc.h"
#include "litestl/util/array.h"
#include "litestl/util/assert.h"
#include "litestl/util/atomicLinkedList.h"
#include "litestl/util/binaryHeap.h"
#include "litestl/util/boolvector.h"
#include "litestl/util/callback_list.h"
#include "litestl/util/compiler_util.h"
#include "litestl/util/concepts.h"
#include "litestl/util/error.h"
#include "litestl/util/function.h"
#include "litestl/util/hash.h"
#include "litestl/util/hash128.h"
#include "litestl/util/index_range.h"
#include "litestl/util/map.h"
#include "litestl/util/memory.h"
#include "litestl/util/ordered_set.h"
#include "litestl/util/pool.h"
#include "litestl/util/rand.h"
#include "litestl/util/rc_buffer.h"
#include "litestl/util/rng.h"
#include "litestl/util/set.h"
#include "litestl/util/sort.h"
#include "litestl/util/span.h"
#include "litestl/util/string.h"
#include "litestl/util/task.h"
#include "litestl/util/time.h"
#include "litestl/util/type_tags.h"
#include "litestl/util/vector.h"
#include "litestl/math/aabb.h"
#include "litestl/math/bspline.h"
#include "litestl/math/color.h"
#include "litestl/math/detmath.h"
#include "litestl/math/euler.h"
#include "litestl/math/geom.h"
#include "litestl/math/math_bindings.h"
#include "litestl/math/matrix.h"
#include "litestl/math/predicates.h"
#include "litestl/math/quat.h"
#include "litestl/math/transform.h"
#include "litestl/math/tri_aabb_isect.h"
#include "litestl/math/vector.h"
#include "litestl/math/wideint.h"
#include "litestl/path/path.h"
#include "litestl/binding/binding.h"
#include "litestl/binding/binding_base.h"
#include "litestl/binding/binding_basic.h"
#include "litestl/binding/binding_bind.h"
#include "litestl/binding/binding_constructor.h"
#include "litestl/binding/binding_constructor_builder.h"
#include "litestl/binding/binding_dispatch.h"
#include "litestl/binding/binding_enum.h"
#include "litestl/binding/binding_literal.h"
#include "litestl/binding/binding_method.h"
#include "litestl/binding/binding_number.h"
#include "litestl/binding/binding_struct.h"
#include "litestl/binding/binding_template.h"
#include "litestl/binding/binding_types.h"
#include "litestl/binding/binding_union.h"
#include "litestl/binding/binding_utils.h"
#include "litestl/binding/manager.h"
#include "litestl/io/binfile.h"
#include "litestl/io/bytes.h"
#include "litestl/io/compress.h"
#include "litestl/io/serial.h"

#include "test_util.h"

test_init;

int main()
{
  /* min/max must still be the windows.h macros here, or this test proves nothing. */
#if !defined(min) || !defined(max)
#error "windows.h min/max macros are not defined"
#endif
  return test_end();
}
