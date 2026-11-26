/* math/detmath.h: deterministic elementary functions.
 *
 *  - Accuracy: under GCC the results are compared with libquadmath (an
 *    offline oracle, linked into this test only) against the bounds the
 *    header states. Other compilers skip this part.
 *  - Determinism: a digest of the output bits over fixed inputs is pinned.
 *    It must be the same under every compiler and platform;
 *    LITESTL_PRINT_GOLDENS=1 prints it.
 *  - Special values and identities, everywhere. */

#include "../math/detmath.h"
#include "splitmix64.h"
#include "test_harness.h"

#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

#ifdef LITESTL_TEST_QUADMATH
#include <quadmath.h>
#endif

test_init;

namespace dm = litestl::math::detmath;
using litestl::test::SplitMix64;

namespace {

uint64_t bits(double x)
{
  uint64_t u;
  memcpy(&u, &x, 8);
  return u;
}

/* inputs: |x| = 2^e (1 + m), e uniform in [lo, hi], random sign if signed */
double draw(SplitMix64 &r, int lo, int hi, bool sign)
{
  const double v = r.finite(lo, hi);
  return sign ? v : std::fabs(v);
}

double uniform(SplitMix64 &r, double a, double b)
{
  return a + (b - a) * r.unit();
}

#ifdef LITESTL_TEST_QUADMATH

struct Stat {
  const char *name;
  double bound;
  bool absolute;
  double worst = 0;
  double worst_x = 0;
  int64_t fails = 0;
  int64_t n = 0;
};

/* error of y against the exact value q, in ulp(q) or absolute */
void record(Stat &s, double x, double y, __float128 q)
{
  s.n++;
  double err;
  if (s.absolute) {
    err = double(fabsq(__float128(y) - q));
  }
  else {
    const double qd = double(q);
    if (!std::isfinite(qd) || std::fabs(qd) < 0x1p-1022) {
      return; /* overflow and subnormal results: no relative bound */
    }
    err = double(fabsq(__float128(y) - q) / __float128(dm::ulp(qd)));
  }
  if (err > s.worst) {
    s.worst = err;
    s.worst_x = x;
  }
  if (!(err <= s.bound)) {
    if (s.fails < 5) {
      fprintf(stderr, "[%s] x = %a: got %a, err %.3g > %.3g\n", s.name, x, y, err, s.bound);
    }
    s.fails++;
  }
}

void report(const Stat &s)
{
  printf("%-14s worst %.3g %s (bound %.3g) at x = %a over %" PRId64 "\n", s.name, s.worst, s.absolute ? "abs" : "ulp",
         s.bound, s.worst_x, s.n);
  test_assert(s.fails == 0);
}

__float128 q_reduce(__float128 x)
{
  const __float128 tp = 2 * M_PIq;
  const __float128 k = roundq(x / tp);
  __float128 r = x - k * tp;
  if (r <= -M_PIq) {
    r += tp;
  }
  if (r > M_PIq) {
    r -= tp;
  }
  return r;
}

void test_accuracy()
{
  const int64_t n = litestl::test::cases(200000);
  SplitMix64 r(0xde7);
  Stat sin_small{"sin |x|<pi/4", dm::kSinCosUlp, false}, cos_small{"cos |x|<pi/4", dm::kSinCosUlp, false};
  Stat sin_big{"sin", dm::kSinCosAbs, true}, cos_big{"cos", dm::kSinCosAbs, true};
  Stat ex{"exp", dm::kExpUlp, false}, lg{"log", dm::kLogUlp, false}, l1p{"log1p", dm::kLog1pUlp, false};
  Stat sh{"sinh", dm::kSinhCoshUlp, false}, ch{"cosh", dm::kSinhCoshUlp, false};
  Stat ash{"asinh", dm::kAsinhUlp, false}, red{"reduce_angle", dm::kReduceAngleAbs, true};
  for (int64_t i = 0; i < n; i++) {
    double x = i % 2 ? uniform(r, -0.785398, 0.785398) : draw(r, -60, -1, true);
    record(sin_small, x, dm::sin(x), sinq(x));
    record(cos_small, x, dm::cos(x), cosq(x));
    /* large arguments, and arguments next to multiples of pi/2 */
    x = i % 3 == 0 ? uniform(r, -dm::kMaxTrigArg, dm::kMaxTrigArg)
                   : (i % 3 == 1 ? uniform(r, -100, 100)
                                 : double(int64_t(r.below(1000000)) - 500000) * 1.5707963267948966 +
                                       uniform(r, -1e-6, 1e-6));
    record(sin_big, x, dm::sin(x), sinq(x));
    record(cos_big, x, dm::cos(x), cosq(x));
    record(red, x, dm::reduce_angle(x), q_reduce(x));

    x = i % 2 ? uniform(r, -745, 709.7) : draw(r, -60, 3, true);
    record(ex, x, dm::exp(x), expq(x));

    x = i % 3 == 0 ? draw(r, -1022, 1023, false) : (i % 3 == 1 ? uniform(r, 0.5, 2) : 1 + uniform(r, -1e-6, 1e-6));
    record(lg, x, dm::log(x), logq(x));

    x = i % 2 ? uniform(r, -0.29, 3) : draw(r, -60, 40, false);
    record(l1p, x, dm::log1p(x), log1pq(x));

    x = i % 2 ? uniform(r, -710, 710) : draw(r, -60, 2, true);
    record(sh, x, dm::sinh(x), sinhq(x));
    record(ch, x, dm::cosh(x), coshq(x));

    x = draw(r, -60, 60, true);
    record(ash, x, dm::asinh(x), asinhq(x));
  }
  for (const Stat *s : {&sin_small, &cos_small, &sin_big, &cos_big, &red, &ex, &lg, &l1p, &sh, &ch, &ash}) {
    report(*s);
  }
}

#endif

/* digest of the output bits over fixed inputs (FNV-1a, 64-bit) */
uint64_t digest()
{
  uint64_t h = 0xcbf29ce484222325ull;
  auto mix = [&](double y) {
    const uint64_t u = bits(y);
    for (int b = 0; b < 8; b++) {
      h = (h ^ ((u >> (8 * b)) & 0xff)) * 0x100000001b3ull;
    }
  };
  SplitMix64 r(0x901d);
  for (int i = 0; i < 20000; i++) {
    const double a = uniform(r, -2000, 2000), b = draw(r, -40, 40, true), c = draw(r, -1000, 1000, false);
    double s, co;
    dm::sincos(a, &s, &co);
    mix(s);
    mix(co);
    mix(dm::reduce_angle(a));
    mix(dm::exp(a * 0.35));
    mix(dm::log(c));
    mix(dm::log1p(std::fabs(b)));
    mix(dm::sinh(a * 0.3));
    mix(dm::cosh(b));
    mix(dm::asinh(b));
  }
  return h;
}

void test_digest()
{
  const uint64_t value = digest();
  const uint64_t expected = 0x01af475f221c2a88ull;
  if (getenv("LITESTL_PRINT_GOLDENS")) {
    printf("detmath digest = 0x%016" PRIx64 "ull\n", value);
    fflush(stdout);
  }
  if (value != expected) {
    fprintf(stderr, "detmath digest changed: 0x%016" PRIx64 "\n", value);
  }
  test_assert(value == expected);
}

void test_special()
{
  const double inf = std::numeric_limits<double>::infinity(), nan = std::numeric_limits<double>::quiet_NaN();
  test_assert(bits(dm::sin(-0.0)) == bits(-0.0) && dm::cos(0.0) == 1.0);
  test_assert(dm::exp(0.0) == 1.0 && dm::log(1.0) == 0.0 && dm::log1p(0.0) == 0.0);
  test_assert(dm::sinh(0.0) == 0.0 && dm::cosh(0.0) == 1.0 && dm::asinh(0.0) == 0.0);
  test_assert(std::isnan(dm::sin(inf)) && std::isnan(dm::cos(nan)) && std::isnan(dm::sin(2 * dm::kMaxTrigArg)));
  test_assert(dm::exp(inf) == inf && dm::exp(-inf) == 0.0 && dm::exp(800) == inf && dm::exp(-800) == 0.0);
  test_assert(dm::log(0.0) == -inf && std::isnan(dm::log(-1.0)) && dm::log(inf) == inf);
  test_assert(dm::log1p(-1.0) == -inf && std::isnan(dm::log1p(-2.0)));
  test_assert(dm::cosh(711) == inf && dm::sinh(-711) == -inf && dm::asinh(-inf) == -inf);
  test_assert(std::isnan(dm::exp(nan)) && std::isnan(dm::asinh(nan)));
  /* odd and even symmetry is exact */
  SplitMix64 r(5);
  for (int i = 0; i < 1000; i++) {
    const double x = uniform(r, -50, 50);
    test_assert(bits(dm::sin(-x)) == bits(-dm::sin(x)) && bits(dm::cos(-x)) == bits(dm::cos(x)));
    test_assert(bits(dm::sinh(-x)) == bits(-dm::sinh(x)) && bits(dm::asinh(-x)) == bits(-dm::asinh(x)));
  }
  /* sin^2 + cos^2 = 1 and exp(log x) = x, to a few ulp */
  for (int i = 0; i < 10000; i++) {
    const double x = uniform(r, -1e5, 1e5);
    double s, c;
    dm::sincos(x, &s, &c);
    test_assert(std::fabs(s * s + c * c - 1.0) <= 8 * 0x1p-53);
    const double y = draw(r, -300, 300, false);
    test_assert(std::fabs(dm::exp(dm::log(y)) - y) <= 1000 * dm::ulp(y));
    const double a = dm::reduce_angle(x);
    test_assert(a > -3.1415926535897931 - 1e-15 && a <= 3.1415926535897931 + 1e-15);
  }
}

} // namespace

int main()
{
  test_special();
#ifdef LITESTL_TEST_QUADMATH
  test_accuracy();
#else
  printf("quad oracle not available with this compiler: accuracy checks skipped\n");
#endif
  test_digest();
  return test_end();
}
