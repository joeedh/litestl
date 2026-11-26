
#include "../math/vector.h"
#include "test_util.h"
#include <cstdio>
#include <cstring>

#include "../util/compiler_util.h"
#include <type_traits>

/* Attribute columns (litestl::attr) store Vec in pages copied with memcpy. */
static_assert(std::is_trivially_copyable_v<litestl::math::float3>);
static_assert(std::is_trivially_copyable_v<litestl::math::double4>);
static_assert(std::is_trivially_copyable_v<litestl::math::int2>);
static_assert(sizeof(litestl::math::float3) == 3 * sizeof(float));
static_assert(sizeof(litestl::math::double4) == 4 * sizeof(double));

test_init;

/* Vec must work with strong scalar types that only convert from integers
 * explicitly (no float literals in the constructors). */
struct StrongScalar {
  int64_t v = 0;
  constexpr StrongScalar() = default;
  constexpr explicit StrongScalar(int64_t x) : v(x) {}
  constexpr bool operator==(const StrongScalar &) const = default;
};
static_assert(litestl::math::Vec<StrongScalar, 3>(StrongScalar(1), StrongScalar(2))[2] == StrongScalar(0));
static_assert(litestl::math::Vec<StrongScalar, 4>(StrongScalar(1), StrongScalar(2), StrongScalar(3))[3] ==
              StrongScalar(0));

int main()
{
  using namespace litestl;
  using namespace litestl::math;

  static_assert(litestl::util::is_simple<litestl::math::float3>());

#if 1
  {
    float3 vec1(1.0), vec2(2.0);

    vec1 -= vec2;

    vec1.print();
    printf(" ");
    vec2.print();
    printf("\n");
  }
#endif

  return test_end();
}
