/* Runs a fixed workload through every determinism-critical API (wide
 * integers, predicates, hash128, rng, parallel_reduce) and hashes all
 * outputs into one digest, compared against a pinned constant. Every build
 * configuration (compiler, flags, LITESTL_NO_INT128, -march, thread count)
 * must produce the same digest. If an intentional change alters it, update
 * the constant in the same commit and say why. LITESTL_PRINT_GOLDENS=1
 * prints the current value. */

#include "litestl/math/predicates.h"
#include "litestl/math/wideint.h"
#include "litestl/util/hash128.h"
#include "litestl/util/rng.h"
#include "litestl/util/task.h"

#include "splitmix64.h"
#include "test_harness.h"

#include <cinttypes>
#include <cmath>
#include <vector>

test_init;

using namespace litestl;
using hash::DomainTag;
using hash::Hash128;
using hash::Hasher128;

int main()
{
  Hasher128 digest(DomainTag::Test);
  test::SplitMix64 rng(20261117);

  /* wide integers */
  for (int i = 0; i < 2000; i++) {
    const int64_t a = int64_t(rng.next()), b = int64_t(rng.next());
    const math::Int128 p = math::mul_wide(a, b);
    const math::Int256 q = math::mul_wide(p, p - math::Int128::from(b));
    const math::Int256 r = (q >> int(rng.below(250))) + (q << int(rng.below(250)));
    for (uint64_t w : r.w) {
      digest.u64(w);
    }
  }

  /* predicates: near-degenerate double inputs (adaptive stages) and lattice */
  for (int i = 0; i < 4096; i++) {
    const double s = std::ldexp(1.0, int(rng.range(-20, 20)));
    const math::double2 c(std::nextafter(0.5 * s, double(i & 63)),
                          std::nextafter(0.5 * s, double(i >> 6)));
    digest.i32(math::pred::orient2d(c, math::double2(12 * s, 12 * s), math::double2(24 * s, 24 * s)));
    const math::double3 u(rng.finite(-5, 5), rng.finite(-5, 5), rng.finite(-5, 5));
    const math::double3 v(rng.finite(-5, 5), rng.finite(-5, 5), rng.finite(-5, 5));
    const double e1 = std::ldexp(1.0, int(rng.range(-40, 40)));
    const double e2 = std::ldexp(1.0, int(rng.range(-40, 40)));
    math::double3 w = v * e2;
    w[int(rng.below(3))] = std::nextafter(w[0], 1e300);
    digest.i32(math::pred::orient3d(u, v, u * e1, w));
    digest.i32(math::pred::incircle(math::double2(u[0], u[1]),
                                    math::double2(u[0] * e1, u[1] * e1),
                                    math::double2(v[0], v[1]),
                                    math::double2(w[0], w[1])));
    digest.i32(math::pred::insphere(u, v, u * e1, v * e2, w));
    const int64_t lim = int64_t(1) << 26;
    auto ri = [&] { return rng.range(-lim, lim); };
    digest.i32(math::pred::insphere(math::int64_3(ri(), ri(), ri()),
                                    math::int64_3(ri(), ri(), ri()),
                                    math::int64_3(ri(), ri(), ri()),
                                    math::int64_3(ri(), ri(), ri()),
                                    math::int64_3(ri(), ri(), ri())));
  }

  /* hash128 */
  for (int i = 0; i < 256; i++) {
    const Hash128 h = Hasher128(DomainTag(1000 + i))
                          .u64(rng.next())
                          .f64_canonical(rng.finite(-100, 100))
                          .str("digest")
                          .finish();
    digest.h128(h);
  }

  /* rng streams */
  const rng::Stream stream(Hasher128(DomainTag::Test).str("digest-stream").finish());
  for (uint64_t e = 0; e < 1000; e++) {
    digest.u32(stream.u32(e, uint32_t(e % 7)));
    digest.f64_bits(stream.uniform(e));
    digest.i64(stream.range(e, -1000000007, 1000000009));
  }

  /* deterministic reduction at several band counts */
  std::vector<double> data(300000);
  for (auto &x : data) {
    x = rng.finite(-30, 30);
  }
  for (int bands : {1, 3, 12}) {
    task::ScopedBandCount scope(bands);
    const double sum = task::parallel_reduce(
        util::IndexRange(int(data.size())),
        1024,
        0.0,
        [&](util::IndexRange r) {
          double s = 0;
          for (int i : r) {
            s += data[size_t(i)];
          }
          return s;
        },
        [](double a, double b) { return a + b; });
    digest.f64_bits(sum);
  }

  const Hash128 value = digest.finish();
  const Hash128 expected = {0x195969b9ce97109aull, 0xfaa8caf7a78cea9full};
  if (getenv("LITESTL_PRINT_GOLDENS")) {
    printf("digest = {0x%016" PRIx64 "ull, 0x%016" PRIx64 "ull}\n", value.lo, value.hi);
    fflush(stdout);
  }
  if (value != expected) {
    fprintf(stderr, "determinism digest changed: %s\n", value.hex().c_str());
  }
  test_assert(value == expected);
  return test_end();
}
