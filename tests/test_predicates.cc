#include "../math/predicates.h"

#include "bigint_oracle.h"
#include "splitmix64.h"
#include "test_harness.h"

#include <array>
#include <cinttypes>
#include <cmath>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <pthread.h>
#define HAVE_PTHREAD_STACK 1
#endif

test_init;

using namespace litestl;
using namespace litestl::math;
using test::BigInt;
using test::FailureLog;
using test::SplitMix64;

/* ------------------------------------------------------------------------
 * Oracle: exact sign of the homogeneous (lifted) determinant, evaluated on
 * the raw coordinates with arbitrary precision integers. This is a different
 * formulation from the translated (difference) form the implementation uses.
 *   orient2d  det [x y 1]          (3x3)
 *   orient3d  det [x y z 1]        (4x4)
 *   incircle  det [x y x2+y2 1]    (4x4)
 *   insphere  det [x y z |p|^2 1]  (5x5)
 * Doubles are scaled by a common 2^k to integers; per-column scaling by a
 * positive factor leaves the sign unchanged.
 * ---------------------------------------------------------------------- */
enum class P { O2, O3, IC, IS };

template <int D> using Pt = std::array<double, D>;

template <int D, size_t N> static int oracle_sign(P kind, const std::array<Pt<D>, N> &pts)
{
  int k = 0;
  for (auto &p : pts) {
    for (double c : p) {
      if (c != 0.0) {
        k = std::max(k, -BigInt::unit_exponent(c));
      }
    }
  }
  std::vector<std::vector<BigInt>> m;
  m.reserve(N);
  for (auto &p : pts) {
    std::vector<BigInt> &row = m.emplace_back();
    row.reserve(D + 2);
    BigInt lift;
    for (double c : p) {
      BigInt v = BigInt::from_double_scaled(c, k);
      lift += v * v;
      row.push_back(std::move(v));
    }
    if (kind == P::IC || kind == P::IS) {
      row.push_back(std::move(lift));
    }
    row.push_back(BigInt::from_i64(1));
  }
  return BigInt::det(m).sign();
}

static int call(P kind, const std::array<Pt<2>, 3> &p)
{
  return pred::orient2d(double2(p[0][0], p[0][1]), double2(p[1][0], p[1][1]), double2(p[2][0], p[2][1]));
}
static int call(P kind, const std::array<Pt<2>, 4> &p)
{
  auto v = [&](int i) { return double2(p[i][0], p[i][1]); };
  return pred::incircle(v(0), v(1), v(2), v(3));
}
static int call(P kind, const std::array<Pt<3>, 4> &p)
{
  auto v = [&](int i) { return double3(p[i][0], p[i][1], p[i][2]); };
  return pred::orient3d(v(0), v(1), v(2), v(3));
}
static int call(P kind, const std::array<Pt<3>, 5> &p)
{
  auto v = [&](int i) { return double3(p[i][0], p[i][1], p[i][2]); };
  return pred::insphere(v(0), v(1), v(2), v(3), v(4));
}

template <int D, size_t N> static void print_pts(const std::array<Pt<D>, N> &pts)
{
  for (auto &p : pts) {
    fprintf(stderr, "(");
    for (int i = 0; i < D; i++) {
      fprintf(stderr, "%s%a", i ? ", " : "", p[i]);
    }
    fprintf(stderr, ") ");
  }
}

/* Per-suite tally of true signs, to check the degenerate suites are balanced. */
struct SignTally {
  int64_t n[3] = {0, 0, 0};
  void add(int s)
  {
    n[s + 1]++;
  }
  void print(const char *name) const
  {
    printf("  %s true signs: -1:%" PRId64 " 0:%" PRId64 " +1:%" PRId64 "\n", name, n[0], n[1], n[2]);
  }
  bool balanced(double min_frac) const
  {
    const double total = double(n[0] + n[1] + n[2]);
    return n[0] >= min_frac * total && n[1] >= min_frac * total && n[2] >= min_frac * total;
  }
};

template <int D, size_t N, typename Gen>
static SignTally run_suite(const char *name, P kind, int64_t count, Gen &&gen, bool metamorphic = true)
{
  FailureLog log(name);
  SignTally tally;
  for (int64_t c = 0; c < count; c++) {
    std::array<Pt<D>, N> pts = gen();
    log.count_case();
    const int expect = oracle_sign<D, N>(kind, pts);
    tally.add(expect);
    const int got = call(kind, pts);
    log.check(got == expect, [&] {
      fprintf(stderr, "got %d expected %d: ", got, expect);
      print_pts<D, N>(pts);
    });
    if (metamorphic) {
      /* swapping the first two points flips the sign */
      std::array<Pt<D>, N> sw = pts;
      std::swap(sw[0], sw[1]);
      const int gs = call(kind, sw);
      log.check(gs == -got, [&] { fprintf(stderr, "swap symmetry: %d vs %d", gs, got); });
      /* cyclic rotation of the first three points (even permutation) */
      std::array<Pt<D>, N> rot = pts;
      std::rotate(rot.begin(), rot.begin() + 1, rot.begin() + 3);
      const int gr = call(kind, rot);
      log.check(gr == got, [&] { fprintf(stderr, "rotation symmetry: %d vs %d", gr, got); });
    }
  }
  log.finish();
  return tally;
}

/* ---------------- generators ---------------- */

static double perturb_ulps(double x, int k)
{
  if (x == 0.0) {
    return x; /* ulps of zero are subnormal: outside the predicates' domain */
  }
  while (k > 0) {
    x = std::nextafter(x, INFINITY);
    k--;
  }
  while (k < 0) {
    x = std::nextafter(x, -INFINITY);
    k++;
  }
  return x;
}

/* Integer lattice points on circles x^2 + y^2 = r^2 via Gaussian-integer
 * products of primitive Pythagorean factors. */
static std::vector<std::array<int64_t, 2>> circle_points(int64_t &r_out)
{
  /* r = 5 * 13 * 17 = 1105 has many lattice points */
  const int64_t r = 1105;
  std::vector<std::array<int64_t, 2>> pts;
  for (int64_t x = -r; x <= r; x++) {
    const int64_t y2 = r * r - x * x;
    const int64_t y = int64_t(std::llround(std::sqrt(double(y2))));
    if (y * y == y2) {
      pts.push_back({x, y});
      if (y) {
        pts.push_back({x, -y});
      }
    }
  }
  r_out = r;
  return pts;
}

/* Integer points on spheres x^2 + y^2 + z^2 = r^2. */
static std::vector<std::array<int64_t, 3>> sphere_points(int64_t &r_out)
{
  const int64_t r = 21;
  std::vector<std::array<int64_t, 3>> pts;
  for (int64_t x = -r; x <= r; x++) {
    for (int64_t y = -r; y <= r; y++) {
      const int64_t z2 = r * r - x * x - y * y;
      if (z2 < 0) {
        continue;
      }
      const int64_t z = int64_t(std::llround(std::sqrt(double(z2))));
      if (z * z == z2) {
        pts.push_back({x, y, z});
        if (z) {
          pts.push_back({x, y, -z});
        }
      }
    }
  }
  r_out = r;
  return pts;
}

struct Generators {
  SplitMix64 rng;
  std::vector<std::array<int64_t, 2>> circ;
  std::vector<std::array<int64_t, 3>> sph;
  int64_t rc = 0, rs = 0;

  explicit Generators(uint64_t seed) : rng(seed)
  {
    circ = circle_points(rc);
    sph = sphere_points(rs);
  }

  double coord()
  {
    return rng.finite(-30, 30);
  }

  template <int D> Pt<D> uniform_pt()
  {
    Pt<D> p;
    for (auto &c : p) {
      c = coord();
    }
    return p;
  }

  int ulps()
  {
    /* ~1/3 exact (zero), rest +-1..3 ulps */
    return rng.below(3) == 0 ? 0 : int(rng.range(1, 3)) * (rng.coin() ? 1 : -1);
  }

  /* Far points exactly on a line (integers up to ~2^40), near point
   * computed by a rounded parametric step, then perturbed by ulps. The
   * far/near magnitude mix makes the coordinate differences inexact, which
   * is what drives the adaptive stages (C, D). */
  std::array<Pt<2>, 3> orient2d_degenerate()
  {
    const double s = std::ldexp(1.0, int(rng.range(-20, 20)));
    Pt<2> a = {double(rng.range(-1000, 1000)) * s, double(rng.range(-1000, 1000)) * s};
    Pt<2> b = {double(rng.range(-(int64_t(1) << 40), int64_t(1) << 40)) * s,
               double(rng.range(-(int64_t(1) << 40), int64_t(1) << 40)) * s};
    if (rng.below(4) == 0) {
      /* Kettner et al. "Classroom Examples": near (0.5,0.5) on y = x */
      a = {12.0 * s, 12.0 * s};
      b = {24.0 * s, 24.0 * s};
      const int ox = int(rng.range(0, 255));
      const int oy = rng.below(3) == 0 ? ox : int(rng.range(0, 255)); /* on the line when equal */
      Pt<2> c = {perturb_ulps(0.5 * s, ox), perturb_ulps(0.5 * s, oy)};
      return {c, a, b};
    }
    const double t = rng.unit() * 2e-6;
    Pt<2> c = {a[0] + t * (b[0] - a[0]), a[1] + t * (b[1] - a[1])};
    c[rng.below(2)] = perturb_ulps(c[rng.below(2)], ulps());
    return {a, b, c};
  }

  std::array<Pt<3>, 4> orient3d_degenerate()
  {
    const double s = std::ldexp(1.0, int(rng.range(-20, 20)));
    auto big = [&] { return double(rng.range(-(int64_t(1) << 40), int64_t(1) << 40)) * s; };
    Pt<3> a = {double(rng.range(-1000, 1000)) * s,
               double(rng.range(-1000, 1000)) * s,
               double(rng.range(-1000, 1000)) * s};
    Pt<3> b = {big(), big(), big()};
    Pt<3> c = {big(), big(), big()};
    if (rng.below(3) == 0) {
      /* exactly coplanar integer configuration (zero sign) */
      const int64_t m = rng.range(-50, 50), n = rng.range(-50, 50);
      Pt<3> d;
      for (int i = 0; i < 3; i++) {
        d[i] = a[i] + double(m) * (b[i] - a[i]) / 64.0 + double(n) * (c[i] - a[i]) / 64.0;
      }
      return {a, b, c, d};
    }
    const double u = rng.unit() * 1e-6, v = rng.unit() * 1e-6;
    Pt<3> d;
    for (int i = 0; i < 3; i++) {
      d[i] = a[i] + u * (b[i] - a[i]) + v * (c[i] - a[i]);
    }
    const int axis = int(rng.below(3));
    d[axis] = perturb_ulps(d[axis], ulps());
    return {a, b, c, d};
  }

  std::array<Pt<2>, 4> incircle_degenerate()
  {
    const int scale_exp = int(rng.range(-10, 25));
    const double s = std::ldexp(1.0, scale_exp);
    const double cx = double(rng.range(-(int64_t(1) << 20), int64_t(1) << 20)) * s;
    const double cy = double(rng.range(-(int64_t(1) << 20), int64_t(1) << 20)) * s;
    auto lattice = [&] {
      auto &q = circ[rng.below(circ.size())];
      return Pt<2>{cx + double(q[0]) * s, cy + double(q[1]) * s};
    };
    Pt<2> a = lattice(), b = lattice(), c = lattice();
    Pt<2> d;
    if (rng.below(3) == 0) {
      d = lattice(); /* exactly cocircular */
    }
    else {
      const double th = rng.unit() * 6.283185307179586;
      d = {cx + double(rc) * s * std::cos(th), cy + double(rc) * s * std::sin(th)};
      d[rng.below(2)] = perturb_ulps(d[rng.below(2)], ulps());
    }
    return {a, b, c, d};
  }

  std::array<Pt<3>, 5> insphere_degenerate()
  {
    const double s = std::ldexp(1.0, int(rng.range(-10, 25)));
    double ctr[3];
    for (double &c : ctr) {
      c = double(rng.range(-(int64_t(1) << 20), int64_t(1) << 20)) * s;
    }
    auto lattice = [&] {
      auto &q = sph[rng.below(sph.size())];
      return Pt<3>{ctr[0] + double(q[0]) * s, ctr[1] + double(q[1]) * s, ctr[2] + double(q[2]) * s};
    };
    Pt<3> a = lattice(), b = lattice(), c = lattice(), d = lattice();
    Pt<3> e;
    if (rng.below(3) == 0) {
      e = lattice();
    }
    else {
      const double th = rng.unit() * 6.283185307179586, z = rng.unit() * 2 - 1;
      const double rr = std::sqrt(1 - z * z);
      e = {ctr[0] + double(rs) * s * rr * std::cos(th),
           ctr[1] + double(rs) * s * rr * std::sin(th),
           ctr[2] + double(rs) * s * z};
      const int axis = int(rng.below(3));
      e[axis] = perturb_ulps(e[axis], ulps());
    }
    return {a, b, c, d, e};
  }

  /* Power-of-two multiples of base vectors: exactly collinear (2D) or
   * coplanar (3D) through the origin, so orient2d/orient3d are exactly zero,
   * and so are incircle (collinear points lift into a vertical plane) and
   * insphere (coplanar points lift into a hyperplane). The wide exponent
   * spread makes the coordinate differences inexact, which forces the
   * adaptive stages; optional perturbations (a few ulps, or log-uniform up to
   * 2^40 ulps) spread the cases over stages B, C and D. */
  int perturbation()
  {
    switch (rng.below(3)) {
      case 0:
        return 0;
      case 1:
        return int(rng.range(1, 3)) * (rng.coin() ? 1 : -1);
      default:
        return int(std::ldexp(1.0, int(rng.range(2, 30)))) * (rng.coin() ? 1 : -1);
    }
  }

  template <int D, size_t N> std::array<Pt<D>, N> span_family()
  {
    Pt<D> u = uniform_pt<D>(), v = uniform_pt<D>();
    for (auto &c : u) {
      c = rng.finite(-10, 10);
    }
    for (auto &c : v) {
      c = rng.finite(-10, 10);
    }
    std::array<Pt<D>, N> pts;
    for (size_t i = 0; i < N; i++) {
      const Pt<D> &base = (D == 2 || i % 2 == 0) ? u : v;
      const double sc = std::ldexp(1.0, int(rng.range(-60, 60)));
      for (int k = 0; k < D; k++) {
        pts[i][k] = base[k] * sc;
      }
    }
    const int p = perturbation();
    if (p) {
      double &c = pts[rng.below(N)][rng.below(D)];
      c = perturb_ulps(c, std::max(-8, std::min(8, p)));
      if (std::abs(p) > 8) {
        c = c * (1.0 + std::ldexp(double(p), -52));
      }
    }
    return pts;
  }

  /* Exactly degenerate lattice configurations (exact coordinate
   * differences) with one coordinate perturbed by a log-uniform 1..4096 ulps:
   * lands many cases just past the static filter, i.e. in stage B. */
  int log_ulps()
  {
    const int k = std::max(1, int(std::ldexp(1.0, 0) * std::exp2(rng.unit() * 12.0)));
    return rng.coin() ? k : -k;
  }

  template <int D, size_t N> std::array<Pt<D>, N> near_filter(P kind)
  {
    const double s = std::ldexp(1.0, int(rng.range(-20, 20)));
    std::array<Pt<D>, N> pts;
    if constexpr (D == 2) {
      if (kind == P::O2) {
        Pt<2> a = {double(rng.range(-(1 << 20), 1 << 20)) * s, double(rng.range(-(1 << 20), 1 << 20)) * s};
        Pt<2> b = {double(rng.range(-(1 << 20), 1 << 20)) * s, double(rng.range(-(1 << 20), 1 << 20)) * s};
        const double t = double(rng.range(-64, 64)) / 64.0;
        pts[0] = a;
        pts[1] = b;
        pts[2] = {a[0] + t * (b[0] - a[0]), a[1] + t * (b[1] - a[1])};
      }
      else {
        /* small centre offset: ulp perturbations stay small relative to the radius */
        const double cx = double(rng.range(-64, 64)) * s, cy = double(rng.range(-64, 64)) * s;
        for (auto &p : pts) {
          auto &q = circ[rng.below(circ.size())];
          p = {cx + double(q[0]) * s, cy + double(q[1]) * s};
        }
      }
    }
    else {
      if (kind == P::O3) {
        Pt<3> a, b, c;
        for (int i = 0; i < 3; i++) {
          a[i] = double(rng.range(-(1 << 20), 1 << 20)) * s;
          b[i] = double(rng.range(-(1 << 20), 1 << 20)) * s;
          c[i] = double(rng.range(-(1 << 20), 1 << 20)) * s;
        }
        const double m = double(rng.range(-64, 64)) / 64.0, n = double(rng.range(-64, 64)) / 64.0;
        pts[0] = a;
        pts[1] = b;
        pts[2] = c;
        for (int i = 0; i < 3; i++) {
          pts[3][i] = a[i] + m * (b[i] - a[i]) + n * (c[i] - a[i]);
        }
      }
      else {
        double ctr[3];
        for (double &c : ctr) {
          c = double(rng.range(-64, 64)) * s;
        }
        for (auto &p : pts) {
          auto &q = sph[rng.below(sph.size())];
          p = {ctr[0] + double(q[0]) * s, ctr[1] + double(q[1]) * s, ctr[2] + double(q[2]) * s};
        }
      }
    }
    /* perturb a nonzero coordinate: ulps of 0 are subnormal, which is outside
     * the documented domain */
    for (int axis = int(rng.below(D)), tries = 0; tries < D; tries++, axis = (axis + 1) % D) {
      double &c = pts[N - 1][axis];
      if (c != 0.0) {
        c = perturb_ulps(c, log_ulps());
        break;
      }
    }
    return pts;
  }

  /* Edge of the documented domain: tiny and huge magnitudes. */
  template <int D, size_t N> std::array<Pt<D>, N> extreme()
  {
    std::array<Pt<D>, N> pts;
    const bool tiny = rng.coin();
    for (auto &p : pts) {
      for (auto &c : p) {
        c = tiny ? rng.finite(-150, -140) : rng.finite(190, 199);
      }
    }
    return pts;
  }
};

/* ---------------- tests ---------------- */

static void test_conventions()
{
  test_assert(pred::orient2d(double2(0, 0), double2(1, 0), double2(0, 1)) == 1);
  test_assert(pred::orient2d(double2(0, 0), double2(0, 1), double2(1, 0)) == -1);
  test_assert(pred::orient2d(double2(0, 0), double2(1, 1), double2(2, 2)) == 0);
  const double3 a(0, 0, 0), b(1, 0, 0), c(0, 1, 0), below(0, 0, -1);
  test_assert(pred::orient3d(a, b, c, below) == 1);
  test_assert(pred::orient3d(a, b, c, double3(0, 0, 1)) == -1);
  test_assert(pred::incircle(double2(0, 0), double2(1, 0), double2(0, 1), double2(0.25, 0.25)) == 1);
  test_assert(pred::incircle(double2(0, 0), double2(1, 0), double2(0, 1), double2(2, 2)) == -1);
  test_assert(pred::incircle(double2(0, 0), double2(1, 0), double2(0, 1), double2(1, 1)) == 0);
  test_assert(pred::insphere(a, b, c, below, double3(0.4, 0.4, -0.4)) == 1);
  test_assert(pred::insphere(a, b, c, below, double3(5, 5, 5)) == -1);
  test_assert(pred::insphere(a, b, c, below, double3(1, 1, 0)) == 0);
  /* float overloads */
  test_assert(pred::orient2d(float2(0, 0), float2(1, 0), float2(0, 1)) == 1);
  test_assert(pred::insphere(float3(0, 0, 0),
                             float3(1, 0, 0),
                             float3(0, 1, 0),
                             float3(0, 0, -1),
                             float3(0.4f, 0.4f, -0.4f)) == 1);
  /* integer versions agree */
  test_assert(pred::orient2d(int64_2(0, 0), int64_2(1, 0), int64_2(0, 1)) == 1);
  test_assert(pred::orient3d(int64_3(0, 0, 0), int64_3(1, 0, 0), int64_3(0, 1, 0), int64_3(0, 0, -1)) == 1);
  test_assert(pred::incircle(int64_2(0, 0), int64_2(4, 0), int64_2(0, 4), int64_2(1, 1)) == 1);
  test_assert(pred::insphere(int64_3(0, 0, 0),
                             int64_3(4, 0, 0),
                             int64_3(0, 4, 0),
                             int64_3(0, 0, -4),
                             int64_3(1, 1, -1)) == 1);
}

static void test_selftest()
{
  const char *what = nullptr;
  const bool ok = pred::fp_selftest(&what);
  if (!ok) {
    fprintf(stderr, "fp_selftest failed: %s\n", what);
  }
  test_assert(ok);
}

static void test_double_suites()
{
  Generators g(1234);
  const int64_t n_uniform = test::cases(50000);
  const int64_t n_degen = test::cases(150000);
  const int64_t n_extreme = test::cases(5000);
  pred::reset_stage_hits();

  run_suite<2, 3>("orient2d uniform", P::O2, n_uniform, [&] {
    return std::array<Pt<2>, 3>{g.uniform_pt<2>(), g.uniform_pt<2>(), g.uniform_pt<2>()};
  });
  run_suite<3, 4>("orient3d uniform", P::O3, n_uniform, [&] {
    return std::array<Pt<3>, 4>{g.uniform_pt<3>(), g.uniform_pt<3>(), g.uniform_pt<3>(), g.uniform_pt<3>()};
  });
  run_suite<2, 4>("incircle uniform", P::IC, n_uniform, [&] {
    return std::array<Pt<2>, 4>{g.uniform_pt<2>(), g.uniform_pt<2>(), g.uniform_pt<2>(), g.uniform_pt<2>()};
  });
  run_suite<3, 5>("insphere uniform", P::IS, n_uniform / 4, [&] {
    return std::array<Pt<3>, 5>{
        g.uniform_pt<3>(), g.uniform_pt<3>(), g.uniform_pt<3>(), g.uniform_pt<3>(), g.uniform_pt<3>()};
  });

  SignTally t[4];
  t[0] = run_suite<2, 3>("orient2d degenerate", P::O2, n_degen, [&] { return g.orient2d_degenerate(); });
  t[1] = run_suite<3, 4>("orient3d degenerate", P::O3, n_degen, [&] { return g.orient3d_degenerate(); });
  t[2] = run_suite<2, 4>("incircle degenerate", P::IC, n_degen, [&] { return g.incircle_degenerate(); });
  t[3] = run_suite<3, 5>("insphere degenerate", P::IS, n_degen / 2, [&] { return g.insphere_degenerate(); });
  SignTally ts[4];
  ts[0] = run_suite<2, 3>("orient2d span family", P::O2, n_degen, [&] { return g.span_family<2, 3>(); });
  ts[1] = run_suite<3, 4>("orient3d span family", P::O3, n_degen, [&] { return g.span_family<3, 4>(); });
  ts[2] = run_suite<2, 4>("incircle span family", P::IC, n_degen, [&] { return g.span_family<2, 4>(); });
  ts[3] = run_suite<3, 5>("insphere span family", P::IS, n_degen / 2, [&] { return g.span_family<3, 5>(); });
  run_suite<2, 3>("orient2d near-filter", P::O2, n_degen / 3, [&] { return g.near_filter<2, 3>(P::O2); });
  run_suite<3, 4>("orient3d near-filter", P::O3, n_degen / 3, [&] { return g.near_filter<3, 4>(P::O3); });
  run_suite<2, 4>("incircle near-filter", P::IC, n_degen / 3, [&] { return g.near_filter<2, 4>(P::IC); });
  run_suite<3, 5>("insphere near-filter", P::IS, n_degen / 6, [&] { return g.near_filter<3, 5>(P::IS); });
  const char *names[4] = {"orient2d", "orient3d", "incircle", "insphere"};
  for (int i = 0; i < 4; i++) {
    for (int k = 0; k < 3; k++) {
      t[i].n[k] += ts[i].n[k];
    }
    t[i].print(names[i]);
    if (!t[i].balanced(0.2)) {
      fprintf(stderr, "%s degenerate suite is not sign-balanced (each sign >= 20%%)\n", names[i]);
    }
    test_assert(t[i].balanced(0.2));
  }

  run_suite<2, 3>("orient2d extreme", P::O2, n_extreme, [&] { return g.extreme<2, 3>(); });
  run_suite<3, 4>("orient3d extreme", P::O3, n_extreme, [&] { return g.extreme<3, 4>(); });
  run_suite<2, 4>("incircle extreme", P::IC, n_extreme, [&] { return g.extreme<2, 4>(); });
  run_suite<3, 5>("insphere extreme", P::IS, n_extreme / 4, [&] { return g.extreme<3, 5>(); });

  /* stage coverage */
  if (pred::stats_enabled()) {
    const char *stages = "ABTCD";
    const int64_t min_hits = 10000 / test::scale_div(); /* reduced runs (sanitizers) scale it down */
    for (int p = 0; p < 4; p++) {
      printf("  %s stage hits:", names[p]);
      for (int s = 0; s < int(pred::Stage::Count); s++) {
        printf(" %c=%" PRIu64, stages[s], pred::stage_hits(pred::Predicate(p), pred::Stage(s)));
      }
      printf("\n");
      for (pred::Stage s : {pred::Stage::A, pred::Stage::B, pred::Stage::C, pred::Stage::D}) {
        const uint64_t h = pred::stage_hits(pred::Predicate(p), s);
        if (h < uint64_t(min_hits)) {
          fprintf(stderr, "%s: stage %c hit only %" PRIu64 " times (< %" PRId64 ")\n", names[p], stages[int(s)], h, min_hits);
        }
        test_assert(h >= uint64_t(min_hits));
      }
    }
  }
  else {
    fprintf(stderr, "stage statistics not compiled in (link predicates_stats)\n");
    test_assert(false);
  }
}

static void test_integer()
{
  Generators g(77);
  const int64_t lim = int64_t(1) << 26;
  auto ri = [&] { return g.rng.range(-lim, lim); };
  auto rs = [&] { return g.rng.below(4) == 0 ? (g.rng.coin() ? lim : -lim) : ri(); };
  FailureLog log("integer predicates vs oracle and vs double");
  const int64_t n = test::cases(100000);
  for (int64_t c = 0; c < n; c++) {
    log.count_case();
    /* random and exactly-degenerate lattice inputs at the full range */
    const bool degen = g.rng.coin();
    int64_2 a(rs(), rs()), b(rs(), rs()), cc(rs(), rs()), d(rs(), rs());
    int64_3 A(rs(), rs(), rs()), B(rs(), rs(), rs()), C(rs(), rs(), rs()), Dd(rs(), rs(), rs()), E(rs(), rs(), rs());
    if (degen) {
      /* collinear / coplanar by construction: midpoint when it is a lattice
       * point, otherwise a duplicated point (both stay in range) */
      cc = b;
      if (((b[0] - a[0]) % 2 == 0) && ((b[1] - a[1]) % 2 == 0)) {
        cc = int64_2(a[0] + (b[0] - a[0]) / 2, a[1] + (b[1] - a[1]) / 2);
      }
      Dd = B;
      if (((B[0] - A[0]) % 2 == 0) && ((B[1] - A[1]) % 2 == 0) && ((B[2] - A[2]) % 2 == 0)) {
        Dd = int64_3(A[0] + (B[0] - A[0]) / 2, A[1] + (B[1] - A[1]) / 2, A[2] + (B[2] - A[2]) / 2);
      }
    }
    auto P2 = [](const int64_2 &p) { return Pt<2>{double(p[0]), double(p[1])}; };
    auto P3 = [](const int64_3 &p) { return Pt<3>{double(p[0]), double(p[1]), double(p[2])}; };

    const int o2 = pred::orient2d(a, b, cc);
    const int o2e = oracle_sign<2, 3>(P::O2, std::array<Pt<2>, 3>{P2(a), P2(b), P2(cc)});
    log.check(o2 == o2e, [&] { fprintf(stderr, "orient2d int %d vs %d", o2, o2e); });
    log.check(o2 == pred::orient2d(double2(a[0], a[1]), double2(b[0], b[1]), double2(cc[0], cc[1])),
              [&] { fprintf(stderr, "orient2d int vs double"); });

    const int o3 = pred::orient3d(A, B, C, Dd);
    const int o3e = oracle_sign<3, 4>(P::O3, std::array<Pt<3>, 4>{P3(A), P3(B), P3(C), P3(Dd)});
    log.check(o3 == o3e, [&] { fprintf(stderr, "orient3d int %d vs %d", o3, o3e); });

    const int ic = pred::incircle(a, b, cc, d);
    const int ice = oracle_sign<2, 4>(P::IC, std::array<Pt<2>, 4>{P2(a), P2(b), P2(cc), P2(d)});
    log.check(ic == ice, [&] { fprintf(stderr, "incircle int %d vs %d", ic, ice); });

    if (c % 4 == 0) {
      const int is = pred::insphere(A, B, C, Dd, E);
      const int ise = oracle_sign<3, 5>(P::IS, std::array<Pt<3>, 5>{P3(A), P3(B), P3(C), P3(Dd), P3(E)});
      log.check(is == ise, [&] { fprintf(stderr, "insphere int %d vs %d", is, ise); });
      const int isd = pred::insphere(double3(A[0], A[1], A[2]),
                                     double3(B[0], B[1], B[2]),
                                     double3(C[0], C[1], C[2]),
                                     double3(Dd[0], Dd[1], Dd[2]),
                                     double3(E[0], E[1], E[2]));
      log.check(is == isd, [&] { fprintf(stderr, "insphere int vs double"); });
    }
  }
  log.finish();

  /* exactly cocircular / cospherical integer configurations */
  FailureLog log2("integer cocircular/cospherical");
  for (int64_t c = 0; c < test::cases(20000); c++) {
    log2.count_case();
    const int64_t sc = int64_t(1) << g.rng.range(0, 14);
    const int64_t ox = g.rng.range(-(int64_t(1) << 24), int64_t(1) << 24);
    const int64_t oy = g.rng.range(-(int64_t(1) << 24), int64_t(1) << 24);
    auto cp = [&] {
      auto &q = g.circ[g.rng.below(g.circ.size())];
      return int64_2(ox + q[0] * sc, oy + q[1] * sc);
    };
    log2.check(pred::incircle(cp(), cp(), cp(), cp()) == 0, [] {});
    auto sp = [&] {
      auto &q = g.sph[g.rng.below(g.sph.size())];
      return int64_3(ox + q[0] * sc, oy + q[1] * sc, ox / 2 + q[2] * sc);
    };
    log2.check(pred::insphere(sp(), sp(), sp(), sp(), sp()) == 0, [] {});
  }
  log2.finish();
}

#ifdef HAVE_PTHREAD_STACK
static void *small_stack_body(void *arg)
{
  Generators g(5);
  int *ok = static_cast<int *>(arg);
  for (int i = 0; i < 2000; i++) {
    auto p = g.insphere_degenerate();
    auto v = [&](int k) { return double3(p[k][0], p[k][1], p[k][2]); };
    const int s = pred::insphere(v(0), v(1), v(2), v(3), v(4));
    *ok &= s == oracle_sign<3, 5>(P::IS, p);
    auto q = g.incircle_degenerate();
    auto w = [&](int k) { return double2(q[k][0], q[k][1]); };
    *ok &= pred::incircle(w(0), w(1), w(2), w(3)) == oracle_sign<2, 4>(P::IC, q);
  }
  return nullptr;
}

static void test_small_stack()
{
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  test_assert(pthread_attr_setstacksize(&attr, 64 * 1024) == 0);
  int ok = 1;
  pthread_t t;
  test_assert(pthread_create(&t, &attr, small_stack_body, &ok) == 0);
  pthread_join(t, nullptr);
  pthread_attr_destroy(&attr);
  test_assert(ok == 1);
}
#endif

int main()
{
  printf("scale=%" PRId64 "\n", test::scale());
  test_selftest();
  test_conventions();
  test_double_suites();
  test_integer();
#ifdef HAVE_PTHREAD_STACK
  test_small_stack();
#endif
  return test_end();
}
