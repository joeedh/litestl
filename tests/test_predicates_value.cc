/* pred::orient3d_value against the exact determinant (BigInt, homogeneous
 * form, as in test_predicates): the relative error must be below
 * orient3d_value_rel = 2^-45, and the value 0 exactly when the determinant is.
 * Suites: uniform; nearly coplanar, with exact and inexact differences;
 * segment-like (a small triangle and a far point, down to grazing angles), in
 * both argument orders; exactly coplanar; extreme exponents; the lattice
 * form (error below 3 epsilon); float equals double. Every stage must run. */

#include "../math/predicates.h"

#include "bigint_oracle.h"
#include "splitmix64.h"
#include "test_harness.h"

#include <cinttypes>
#include <cmath>

test_init;

using namespace litestl;
using namespace litestl::math;
using test::BigInt;
using test::FailureLog;
using test::SplitMix64;

namespace {

struct Quad {
  double p[4][3];
};

BigInt abs_of(const BigInt &x)
{
  return x.sign() < 0 ? -x : x;
}

/* orient3d exactly: the 4x4 determinant with a column of ones equals the 3x3
 * determinant of the rows' differences from the last row. */
BigInt det_rows(const BigInt (&m)[4][3])
{
  BigInt d[3][3];
  for (int i = 0; i < 3; i++) {
    for (int c = 0; c < 3; c++) {
      d[i][c] = m[i][c] - m[3][c];
    }
  }
  return d[0][0] * (d[1][1] * d[2][2] - d[1][2] * d[2][1]) - d[0][1] * (d[1][0] * d[2][2] - d[1][2] * d[2][0]) +
         d[0][2] * (d[1][0] * d[2][1] - d[1][1] * d[2][0]);
}

/* The exact determinant times 2^shift, where shift = 3k and the coordinates
 * are multiples of 2^-k. */
BigInt exact_scaled(const Quad &q, int &shift)
{
  int k = 0;
  for (const auto &p : q.p) {
    for (const double c : p) {
      if (c != 0.0) {
        k = std::max(k, -BigInt::unit_exponent(c));
      }
    }
  }
  BigInt m[4][3];
  for (int j = 0; j < 4; j++) {
    for (int c = 0; c < 3; c++) {
      m[j][c] = BigInt::from_double_scaled(q.p[j][c], k);
    }
  }
  shift = 3 * k;
  return det_rows(m);
}

/* The largest b <= 60 with |v - exact| < 2^-b |exact|; 61 if v is exact; -1
 * if the signs differ. */
int rel_bits(double v, const BigInt &e, int shift)
{
  if (v == 0.0 || e.sign() == 0) {
    return v == 0.0 && e.sign() == 0 ? 61 : -1;
  }
  if ((v > 0) != (e.sign() > 0)) {
    return -1;
  }
  const int m = std::max(shift, -BigInt::unit_exponent(v));
  const BigInt V = BigInt::from_double_scaled(v, m), E = e.shl(m - shift);
  const BigInt d = abs_of(V - E), ae = abs_of(E);
  if (d.sign() == 0) {
    return 61;
  }
  for (int b = 60; b >= 0; b--) {
    if (d.shl(b).cmp(ae) < 0) {
      return b;
    }
  }
  return 0;
}

double3 pt(const Quad &q, int i)
{
  return double3(q.p[i][0], q.p[i][1], q.p[i][2]);
}

double perturb_ulps(double x, int k)
{
  if (x == 0.0) {
    return x;
  }
  for (; k > 0; k--) {
    x = std::nextafter(x, INFINITY);
  }
  for (; k < 0; k++) {
    x = std::nextafter(x, -INFINITY);
  }
  return x;
}

struct Suite {
  FailureLog log;
  int worst = 61;
  int64_t zeros = 0;
  const char *name;
  explicit Suite(const char *n) : log(n), name(n) {}

  void run(const Quad &q)
  {
    log.count_case();
    int shift = 0;
    const BigInt e = exact_scaled(q, shift);
    const double v = pred::orient3d_value(pt(q, 0), pt(q, 1), pt(q, 2), pt(q, 3));
    const int b = rel_bits(v, e, shift);
    zeros += e.sign() == 0;
    worst = std::min(worst, b);
    log.check(b >= 45 && (v > 0 ? 1 : v < 0 ? -1 : 0) == pred::orient3d(pt(q, 0), pt(q, 1), pt(q, 2), pt(q, 3)), [&] {
      fprintf(stderr, "relative error 2^-%d, value %a, exact sign %d:", b, v, e.sign());
      for (const auto &p : q.p) {
        fprintf(stderr, " (%a, %a, %a)", p[0], p[1], p[2]);
      }
    });
  }
  void finish()
  {
    printf("  %s: worst relative error below 2^-%d, %" PRId64 " exact zeros\n", name, worst, zeros);
    log.finish();
  }
};

/* a small triangle at x and a far point at grazing angle 2^-g */
Quad segment_like(SplitMix64 &r, bool far_first, int max_g = 60)
{
  const double s = std::ldexp(1.0, int(r.range(-10, 5)));
  double x[3], u[3], w[3], n[3];
  for (int i = 0; i < 3; i++) {
    x[i] = r.finite(-5, 12);
    u[i] = (r.unit() - 0.5) * s;
    w[i] = (r.unit() - 0.5) * s;
  }
  n[0] = u[1] * w[2] - u[2] * w[1];
  n[1] = u[2] * w[0] - u[0] * w[2];
  n[2] = u[0] * w[1] - u[1] * w[0];
  const double nl = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
  const double len = s * std::ldexp(1.0, int(r.range(0, 30)));
  const double g = std::ldexp(1.0, -int(r.range(0, max_g))) * (r.coin() ? 1 : -1);
  const double a = r.unit() - 0.5, b = r.unit() - 0.5;
  Quad q;
  for (int i = 0; i < 3; i++) {
    const double tri[3] = {x[i], x[i] + u[i], x[i] + w[i]};
    const double far = x[i] + len * (a * u[i] / s + b * w[i] / s + g * n[i] / nl);
    if (far_first) {
      q.p[0][i] = far;
      q.p[1][i] = tri[1];
      q.p[2][i] = tri[2];
      q.p[3][i] = tri[0];
    }
    else {
      q.p[0][i] = tri[0];
      q.p[1][i] = tri[1];
      q.p[2][i] = tri[2];
      q.p[3][i] = far;
    }
  }
  return q;
}

/* b, c large; d = a + u (b - a) + v (c - a), a few ulps off; with integer
 * coordinates scaled by 2^e, differences are often exact */
Quad near_coplanar(SplitMix64 &r, bool integers)
{
  const double s = std::ldexp(1.0, int(r.range(-20, 20)));
  const auto big = [&] { return double(r.range(-(int64_t(1) << 40), int64_t(1) << 40)) * s; };
  Quad q;
  for (int i = 0; i < 3; i++) {
    q.p[0][i] = double(r.range(-1000, 1000)) * s;
    q.p[1][i] = big();
    q.p[2][i] = big();
  }
  if (integers) {
    const int64_t m = r.range(-50, 50), n = r.range(-50, 50);
    for (int i = 0; i < 3; i++) {
      q.p[3][i] = q.p[0][i] + double(m) * (q.p[1][i] - q.p[0][i]) / 64.0 + double(n) * (q.p[2][i] - q.p[0][i]) / 64.0;
    }
    if (r.below(3)) {
      const int axis = int(r.below(3));
      q.p[3][axis] = perturb_ulps(q.p[3][axis], int(r.range(-4, 4)));
    }
    return q;
  }
  const double u = r.unit() * 1e-6, v = r.unit() * 1e-6;
  for (int i = 0; i < 3; i++) {
    q.p[3][i] = q.p[0][i] + u * (q.p[1][i] - q.p[0][i]) + v * (q.p[2][i] - q.p[0][i]);
  }
  const int axis = int(r.below(3));
  q.p[3][axis] = perturb_ulps(q.p[3][axis], int(r.range(-8, 8)));
  return q;
}

Quad scaled(Quad q, int e)
{
  for (auto &p : q.p) {
    for (double &c : p) {
      c = std::ldexp(c, e);
    }
  }
  return q;
}

} // namespace

int main()
{
  SplitMix64 r(45);
  const int64_t n = test::cases(20000);
  pred::reset_stage_hits();
  {
    Suite s("uniform");
    for (int64_t i = 0; i < n; i++) {
      Quad q;
      for (auto &p : q.p) {
        for (double &c : p) {
          c = r.finite(-10, 10);
        }
      }
      s.run(q);
    }
    s.finish();
  }
  {
    Suite s("nearly coplanar, inexact differences");
    for (int64_t i = 0; i < n; i++) {
      s.run(near_coplanar(r, false));
    }
    s.finish();
  }
  {
    Suite s("nearly or exactly coplanar, integers");
    for (int64_t i = 0; i < n; i++) {
      s.run(near_coplanar(r, true));
    }
    s.finish();
    test_assert(s.zeros > n / 5);
  }
  {
    /* angles down to 2^-60; then, at angles of 2^-3 and more, the filter
     * decides most with a triangle vertex last, few with the far point last */
    Suite s("segment-like, far point last");
    Suite s2("segment-like, far point first");
    for (int64_t i = 0; i < n; i++) {
      s.run(segment_like(r, false));
      s2.run(segment_like(r, true));
    }
    s.finish();
    s2.finish();
    uint64_t a_hits[2];
    for (int order = 0; order < 2; order++) {
      const uint64_t before = pred::stage_hits(pred::Predicate::Orient3dValue, pred::Stage::A);
      SplitMix64 rs(46);
      for (int64_t i = 0; i < n; i++) {
        (order ? s2 : s).run(segment_like(rs, order == 1, 3));
      }
      a_hits[order] = pred::stage_hits(pred::Predicate::Orient3dValue, pred::Stage::A) - before;
    }
    printf("  segment-like, angles >= 2^-3: the filter decides %" PRIu64 " of %" PRId64
           " with the far point last, %" PRIu64 " with it first\n",
           a_hits[0], n, a_hits[1]);
    test_assert(!pred::stats_enabled() || (a_hits[1] > uint64_t(n) * 3 / 4 && a_hits[0] < uint64_t(n) / 4));
  }
  {
    Suite s("extreme exponents");
    for (int64_t i = 0; i < n / 2; i++) {
      s.run(scaled(near_coplanar(r, r.coin()), int(r.range(-100, 120))));
    }
    s.finish();
  }

  /* the lattice: exact Int128 determinants, converted */
  {
    FailureLog log("lattice");
    int worst = 61;
    const int64_t lim = int64_t(1) << 30;
    for (int64_t i = 0; i < n; i++) {
      log.count_case();
      int64_t p[4][3];
      for (int j = 0; j < 3; j++) {
        for (int c = 0; c < 3; c++) {
          p[j][c] = r.range(-lim / 2, lim / 2);
        }
      }
      const int64_t m = r.range(-64, 64), k = r.range(-64, 64), noise = r.below(2) ? r.range(-3, 3) : 0;
      for (int c = 0; c < 3; c++) {
        p[3][c] = p[0][c] + (m * (p[1][c] - p[0][c]) + k * (p[2][c] - p[0][c])) / 256 + (c == 0 ? noise : 0);
        p[3][c] = std::max(-lim, std::min(lim, p[3][c]));
      }
      BigInt mat[4][3];
      for (int j = 0; j < 4; j++) {
        for (int c = 0; c < 3; c++) {
          mat[j][c] = BigInt::from_i64(p[j][c]);
        }
      }
      const BigInt e = det_rows(mat);
      const auto v3 = [&](int j) { return int64_3(p[j][0], p[j][1], p[j][2]); };
      const double v = pred::orient3d_value<30>(v3(0), v3(1), v3(2), v3(3));
      const int b = rel_bits(v, e, 0);
      worst = std::min(worst, b);
      log.check(b >= 51, [&] { fprintf(stderr, "relative error 2^-%d", b); });
    }
    printf("  lattice: worst relative error below 2^-%d\n", worst);
    log.finish();
  }

  /* float is double on the widened points */
  {
    FailureLog log("float");
    for (int64_t i = 0; i < n / 4; i++) {
      log.count_case();
      float3 f[4];
      for (auto &p : f) {
        for (int c = 0; c < 3; c++) {
          p[c] = float(r.range(-1000, 1000)) * 0.125f;
        }
      }
      const double v = pred::orient3d_value(f[0], f[1], f[2], f[3]);
      const auto w = [&](int j) { return double3(f[j][0], f[j][1], f[j][2]); };
      log.check(v == pred::orient3d_value(w(0), w(1), w(2), w(3)), [&] { fprintf(stderr, "float differs"); });
    }
    log.finish();
  }

  if (pred::stats_enabled()) {
    const char *stages = "ABTCD";
    printf("  stage hits:");
    for (int s = 0; s < int(pred::Stage::Count); s++) {
      const uint64_t h = pred::stage_hits(pred::Predicate::Orient3dValue, pred::Stage(s));
      printf(" %c=%" PRIu64, stages[s], h);
      test_assert(h >= uint64_t(n / 100));
    }
    printf("\n");
  }
  else {
    fprintf(stderr, "stage statistics not compiled in (link predicates_stats)\n");
    test_assert(false);
  }
  return test_end();
}
