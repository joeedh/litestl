#pragma once

/* Test-local input generator (SplitMix64, Steele/Lea/Flood 2014).
 *
 * Deliberately independent of litestl::rng so that tests of the predicates,
 * wide integers etc. don't depend on the RNG under test. */

#include <cmath>
#include <cstdint>
#include <cstring>

namespace litestl::test {

struct SplitMix64 {
  uint64_t s;

  explicit SplitMix64(uint64_t seed) : s(seed) {}

  uint64_t next()
  {
    uint64_t z = (s += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
  }

  /* Uniform in [0, n) for n > 0 (slight modulo bias is irrelevant for tests). */
  uint64_t below(uint64_t n)
  {
    return next() % n;
  }

  /* Uniform integer in [lo, hi]. */
  int64_t range(int64_t lo, int64_t hi)
  {
    return lo + int64_t(below(uint64_t(hi - lo) + 1));
  }

  bool coin()
  {
    return next() & 1;
  }

  /* Uniform double in [0, 1). */
  double unit()
  {
    return double(next() >> 11) * 0x1.0p-53;
  }

  /* Random finite double: random sign, full 52-bit mantissa, binary exponent
   * uniform in [min_exp, max_exp] (value in [2^e, 2^(e+1))). */
  double finite(int min_exp, int max_exp)
  {
    uint64_t mant = next() & ((uint64_t(1) << 52) - 1);
    double m = 1.0 + double(mant) * 0x1.0p-52;
    int e = int(range(min_exp, max_exp));
    double v = std::ldexp(m, e);
    return coin() ? -v : v;
  }
};

} // namespace litestl::test
