/* pred::incircle_sos: the symbolically perturbed incircle, against an
 * independent formal-polynomial oracle (big-integer cofactors, compared
 * lexicographically in eps, so no eps value is chosen). */

#include "litestl/math/predicates.h"

#include "bigint_oracle.h"
#include "splitmix64.h"
#include "test_harness.h"

#include <vector>

test_init;

using namespace litestl;
using namespace litestl::math;
using test::BigInt;

/* det of rows [x, y, x^2 + y^2, 1] for the 4 points, and the cofactors of the
 * lift column (the eps coefficients). */
static int oracle(const int64_t (&p)[4][2], const uint64_t (&rank)[4], int *d0_sign)
{
  auto row = [&](int i) {
    const BigInt x = BigInt::from_i64(p[i][0]), y = BigInt::from_i64(p[i][1]);
    return std::vector<BigInt>{x, y, x * x + y * y, BigInt::from_i64(1)};
  };
  std::vector<std::vector<BigInt>> m;
  for (int i = 0; i < 4; i++) {
    m.push_back(row(i));
  }
  *d0_sign = BigInt::det(m).sign();
  if (*d0_sign != 0) {
    return *d0_sign;
  }
  int coef[4];
  for (int i = 0; i < 4; i++) {
    std::vector<std::vector<BigInt>> minor;
    for (int r = 0; r < 4; r++) {
      if (r == i) {
        continue;
      }
      minor.push_back({m[r][0], m[r][1], m[r][3]});
    }
    const int sgn = ((i + 2) % 2 == 0) ? 1 : -1;
    coef[i] = sgn * BigInt::det(minor).sign();
  }
  /* the lowest power of eps is the smallest rank */
  int order[4] = {0, 1, 2, 3};
  std::sort(order, order + 4, [&](int x, int y) { return rank[x] < rank[y]; });
  for (int k = 0; k < 4; k++) {
    if (coef[order[k]] != 0) {
      return coef[order[k]];
    }
  }
  return 0;
}

int main()
{
  test::SplitMix64 r(5);
  test::FailureLog log("incircle_sos");
  int ties = 0, d0_mismatch = 0;
  const int64_t ncases = test::cases(200000);
  for (int64_t it = 0; it < ncases; it++) {
    int64_t p[4][2];
    bool distinct = true;
    const int64_t scale = (it % 2) ? 1 : (int64_t(1) << 20); /* scaling keeps cocircularity */
    for (int i = 0; i < 4; i++) {
      p[i][0] = r.range(-3, 3) * scale;
      p[i][1] = r.range(-3, 3) * scale;
      for (int j = 0; j < i; j++) {
        distinct = distinct && !(p[i][0] == p[j][0] && p[i][1] == p[j][1]);
      }
    }
    if (!distinct) {
      continue;
    }
    uint64_t rank[4] = {0, 1, 2, 3};
    for (int i = 3; i > 0; i--) {
      std::swap(rank[i], rank[r.below(uint64_t(i + 1))]);
    }
    int d0;
    const int expect = oracle(p, rank, &d0);
    ties += d0 == 0;
    const int64_2 a(p[0][0], p[0][1]), b(p[1][0], p[1][1]), c(p[2][0], p[2][1]), d(p[3][0], p[3][1]);
    /* the 4x4 lifted determinant has the sign of Shewchuk's incircle */
    d0_mismatch += pred::incircle<26>(a, b, c, d) != d0;
    const int gi = pred::incircle_sos<26>(a, b, c, d, rank[0], rank[1], rank[2], rank[3]);
    auto dp = [&](int i) { return double2(double(p[i][0]), double(p[i][1])); };
    const double2 ad = dp(0), bd = dp(1), cd = dp(2), dd = dp(3);
    const int gd = pred::incircle_sos(ad, bd, cd, dd, rank[0], rank[1], rank[2], rank[3]);
    log.check(gi == expect && gd == expect, [&] {
      fprintf(stderr, "pts (%lld,%lld) (%lld,%lld) (%lld,%lld) (%lld,%lld) ranks %llu %llu %llu %llu: got %d/%d expect %d",
              (long long)p[0][0], (long long)p[0][1], (long long)p[1][0], (long long)p[1][1], (long long)p[2][0],
              (long long)p[2][1], (long long)p[3][0], (long long)p[3][1], (unsigned long long)rank[0],
              (unsigned long long)rank[1], (unsigned long long)rank[2], (unsigned long long)rank[3], gi, gd, expect);
    });
    /* nonzero unless all four points are collinear */
    const bool collinear = pred::orient2d<26>(a, b, c) == 0 && pred::orient2d<26>(a, b, d) == 0;
    log.check(collinear || gi != 0, [&] { fprintf(stderr, "zero result on a non-collinear quadruple"); });
  }
  log.finish();
  test_assert(d0_mismatch == 0);
  test_assert(ties > 1000); /* the grid produces many exact cocircular ties */
  return test_end();
}
