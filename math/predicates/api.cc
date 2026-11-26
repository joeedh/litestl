#include "expansion.h"

#include <atomic>
#include <cmath>

namespace litestl::math::pred {

namespace {
int sign(double d)
{
  return d > 0.0 ? 1 : (d < 0.0 ? -1 : 0);
}

#ifndef NDEBUG
/* Documented domain: 0 or |x| in [2^-150, 2^200] (see predicates.h). */
void check_domain(double v)
{
  const double a = std::fabs(v);
  if (!(v == 0.0 || (a >= 0x1.0p-150 && a <= 0x1.0p200))) {
    fprintf(stderr, "litestl::math::pred: input %a outside the exact domain\n", v);
    abort();
  }
}
template <int N> void check_domain(const Vec<double, N> &p)
{
  for (int i = 0; i < N; i++) {
    check_domain(p[i]);
  }
}
#define CHECK_DOMAIN(...) [&] { for (const auto *p_ : {__VA_ARGS__}) check_domain(*p_); }()
#else
#define CHECK_DOMAIN(...) ((void)0)
#endif

template <int N> Vec<double, N> widen(const Vec<float, N> &v)
{
  Vec<double, N> r;
  for (int i = 0; i < N; i++) {
    r[i] = double(v[i]);
  }
  return r;
}
} // namespace

int orient2d(const double2 &a, const double2 &b, const double2 &c)
{
  CHECK_DOMAIN(&a, &b, &c);
  const double pa[2] = {a[0], a[1]}, pb[2] = {b[0], b[1]}, pc[2] = {c[0], c[1]};
  return sign(detail::orient2d(pa, pb, pc));
}

int orient3d(const double3 &a, const double3 &b, const double3 &c, const double3 &d)
{
  CHECK_DOMAIN(&a, &b, &c, &d);
  const double pa[3] = {a[0], a[1], a[2]}, pb[3] = {b[0], b[1], b[2]};
  const double pc[3] = {c[0], c[1], c[2]}, pd[3] = {d[0], d[1], d[2]};
  return sign(detail::orient3d(pa, pb, pc, pd));
}

double orient3d_value(const double3 &a, const double3 &b, const double3 &c, const double3 &d)
{
  CHECK_DOMAIN(&a, &b, &c, &d);
  const double pa[3] = {a[0], a[1], a[2]}, pb[3] = {b[0], b[1], b[2]};
  const double pc[3] = {c[0], c[1], c[2]}, pd[3] = {d[0], d[1], d[2]};
  return detail::orient3d_value(pa, pb, pc, pd);
}

int incircle(const double2 &a, const double2 &b, const double2 &c, const double2 &d)
{
  CHECK_DOMAIN(&a, &b, &c, &d);
  const double pa[2] = {a[0], a[1]}, pb[2] = {b[0], b[1]};
  const double pc[2] = {c[0], c[1]}, pd[2] = {d[0], d[1]};
  return sign(detail::incircle(pa, pb, pc, pd));
}

int insphere(
    const double3 &a, const double3 &b, const double3 &c, const double3 &d, const double3 &e)
{
  CHECK_DOMAIN(&a, &b, &c, &d, &e);
  const double pa[3] = {a[0], a[1], a[2]}, pb[3] = {b[0], b[1], b[2]};
  const double pc[3] = {c[0], c[1], c[2]}, pd[3] = {d[0], d[1], d[2]};
  const double pe[3] = {e[0], e[1], e[2]};
  return sign(detail::insphere(pa, pb, pc, pd, pe));
}

int orient2d(const float2 &a, const float2 &b, const float2 &c)
{
  return orient2d(widen(a), widen(b), widen(c));
}
int orient3d(const float3 &a, const float3 &b, const float3 &c, const float3 &d)
{
  return orient3d(widen(a), widen(b), widen(c), widen(d));
}
double orient3d_value(const float3 &a, const float3 &b, const float3 &c, const float3 &d)
{
  return orient3d_value(widen(a), widen(b), widen(c), widen(d));
}
int incircle(const float2 &a, const float2 &b, const float2 &c, const float2 &d)
{
  return incircle(widen(a), widen(b), widen(c), widen(d));
}
int insphere(const float3 &a, const float3 &b, const float3 &c, const float3 &d, const float3 &e)
{
  return insphere(widen(a), widen(b), widen(c), widen(d), widen(e));
}

/* ---------------- diagnostics ---------------- */

#ifdef LITESTL_PRED_STATS
namespace {
std::atomic<uint64_t> g_hits[int(Predicate::Count)][int(Stage::Count)];
}
void detail::stat_hit(Predicate p, Stage s)
{
  g_hits[int(p)][int(s)].fetch_add(1, std::memory_order_relaxed);
}
uint64_t stage_hits(Predicate p, Stage s)
{
  return g_hits[int(p)][int(s)].load(std::memory_order_relaxed);
}
void reset_stage_hits()
{
  for (auto &row : g_hits) {
    for (auto &h : row) {
      h.store(0, std::memory_order_relaxed);
    }
  }
}
bool stats_enabled()
{
  return true;
}
#else
uint64_t stage_hits(Predicate, Stage)
{
  return 0;
}
void reset_stage_hits() {}
bool stats_enabled()
{
  return false;
}
#endif

namespace {
/* volatile loads keep the compiler from constant-folding the probes */
double load(const volatile double &v)
{
  return v;
}
} // namespace

bool fp_selftest(const char **what)
{
  /* FMA contraction: a*b+c rounds a*b first when not contracted.
   * (1+2^-30)^2 = 1 + 2^-29 + 2^-60; rounded product = 1 + 2^-29, so the
   * uncontracted result is exactly 0; a fused one gives 2^-60. */
  static volatile double va = 1.0 + 0x1.0p-30, vc = -(1.0 + 0x1.0p-29);
  const double a = load(va), c = load(vc);
  const double r = a * a + c;
  if (r != 0.0) {
    if (what) {
      *what = "a*b+c was contracted into an FMA";
    }
    return false;
  }
  /* Reassociation: Two_Sum's tail must recover the rounding error exactly. */
  static volatile double vbig = 0x1.0p53, vone = 1.0;
  double x, y;
  detail::Two_Sum(load(vbig), load(vone), x, y);
  if (x != 0x1.0p53 || y != 1.0) {
    if (what) {
      *what = "Two_Sum tail is wrong (reassociation / excess precision)";
    }
    return false;
  }
  /* Dekker split product must be exact */
  detail::Two_Product(a, a, x, y);
  if (x != 1.0 + 0x1.0p-29 || y != 0x1.0p-60) {
    if (what) {
      *what = "Two_Product tail is wrong";
    }
    return false;
  }
  return true;
}

} // namespace litestl::math::pred
