/* Embedding check (Windows only): every public header must compile after <windows.h>
 * without NOMINMAX, and next to Unreal-style check/verify/ensure macros. Excluded:
 * util/wasm.h (WASM only), and math/lut.h and math/mix.h (unused and do not compile on
 * their own). */
#include <windows.h>
#define check(expr) ((void)(expr))
#define verify(expr) ((void)(expr))
#define ensure(expr) (!!(expr))

#include "../util/alloc.h"
#include "../util/array.h"
#include "../util/assert.h"
#include "../util/atomicLinkedList.h"
#include "../util/binaryHeap.h"
#include "../util/boolvector.h"
#include "../util/callback_list.h"
#include "../util/compiler_util.h"
#include "../util/concepts.h"
#include "../util/error.h"
#include "../util/function.h"
#include "../util/hash.h"
#include "../util/hash128.h"
#include "../util/index_range.h"
#include "../util/map.h"
#include "../util/memory.h"
#include "../util/ordered_set.h"
#include "../util/pool.h"
#include "../util/rand.h"
#include "../util/rc_buffer.h"
#include "../util/rng.h"
#include "../util/set.h"
#include "../util/sort.h"
#include "../util/span.h"
#include "../util/string.h"
#include "../util/task.h"
#include "../util/time.h"
#include "../util/type_tags.h"
#include "../util/vector.h"
#include "../math/aabb.h"
#include "../math/bspline.h"
#include "../math/color.h"
#include "../math/detmath.h"
#include "../math/euler.h"
#include "../math/geom.h"
#include "../math/math_bindings.h"
#include "../math/matrix.h"
#include "../math/matrix_bindings.h"
#include "../math/predicates.h"
#include "../math/quat.h"
#include "../math/transform.h"
#include "../math/tri_aabb_isect.h"
#include "../math/vector.h"
#include "../math/wideint.h"
#include "../path/path.h"
#include "../binding/binding.h"
#include "../binding/binding_base.h"
#include "../binding/binding_basic.h"
#include "../binding/binding_bind.h"
#include "../binding/binding_constructor.h"
#include "../binding/binding_constructor_builder.h"
#include "../binding/binding_dispatch.h"
#include "../binding/binding_enum.h"
#include "../binding/binding_literal.h"
#include "../binding/binding_method.h"
#include "../binding/binding_number.h"
#include "../binding/binding_struct.h"
#include "../binding/binding_template.h"
#include "../binding/binding_types.h"
#include "../binding/binding_union.h"
#include "../binding/binding_utils.h"
#include "../binding/manager.h"
#include "../io/binfile.h"
#include "../io/bytes.h"
#include "../io/compress.h"
#include "../io/serial.h"

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
