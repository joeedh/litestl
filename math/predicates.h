#pragma once

/* Robust geometric predicates. Every function returns the exact sign
 * (-1, 0, +1) of its determinant.
 *
 * Sign conventions (Shewchuk):
 *   orient2d(a,b,c)     > 0  iff a, b, c are counter-clockwise
 *   orient3d(a,b,c,d)   > 0  iff d lies below the plane through a, b, c, where
 *                            a, b, c appear counter-clockwise seen from above
 *   incircle(a,b,c,d)   > 0  iff d is inside the circle through
 *                            counter-clockwise a, b, c
 *   insphere(a,b,c,d,e) > 0  iff e is inside the sphere through a, b, c, d,
 *                            where orient3d(a,b,c,d) > 0
 *
 * double: adaptive-precision exact arithmetic (port of Shewchuk 1997, see
 * math/predicates/). Exact for finite inputs that are 0 or have |x| in
 * [2^-150, 2^200]; that keeps every exact product component >= 2^-1022, so
 * results stay exact even with FTZ/DAZ enabled. Checked in debug builds.
 * Stack use: <= ~33 KB (insphere); the exact insphere stage uses ~180 KB of
 * heap scratch instead of stack.
 *
 * float: converted to double (exactly) first; no extra domain restriction.
 *
 * int64 lattice: exact integer determinants for |x| <= 2^Bits (default 26),
 * checked unless LITESTL_PRED_CHECK_RANGE is 0. Accumulator widths follow
 * Hadamard's bound on (Bits+1)-bit coordinate differences:
 *   orient2d <= 2^(2B+3)       (B=26: 55 bits  -> int64)
 *   orient3d ~= 2^(3B+6.4)     (B=26: 85 bits  -> Int128)
 *   incircle ~= 2^(4B+7.4)     (B=26: 113 bits -> Int128)
 *   insphere ~= 2^(5B+10.6)    (B=26: 142 bits -> Int256)
 * Intermediates may wrap harmlessly; only the final value must fit. */

#include "vector.h"
#include "wideint.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#ifndef LITESTL_PRED_CHECK_RANGE
#define LITESTL_PRED_CHECK_RANGE 1
#endif

namespace litestl::math {
using int64_2 = Vec<int64_t, 2>;
using int64_3 = Vec<int64_t, 3>;
using int64_4 = Vec<int64_t, 4>;
} // namespace litestl::math

namespace litestl::math::pred {

int orient2d(const double2 &a, const double2 &b, const double2 &c);
int orient3d(const double3 &a, const double3 &b, const double3 &c, const double3 &d);
int incircle(const double2 &a, const double2 &b, const double2 &c, const double2 &d);
int insphere(const double3 &a,
             const double3 &b,
             const double3 &c,
             const double3 &d,
             const double3 &e);

/** The value of orient3d's determinant, with a relative error below
 * orient3d_value_rel: |v - det| < 2^-45 |det|. So its sign is orient3d's, and
 * it is 0 exactly when the determinant is. For quotients of determinants,
 * such as where a segment crosses a plane, which then stay accurate too. The
 * floating filter decides most calls when the determinant isn't small next to
 * its permanent; others take orient3d's adaptive stages or, rarely, the exact
 * expansion. Put the point that is closest to the others last: the filter is
 * relative to products of differences from it. */
double orient3d_value(const double3 &a, const double3 &b, const double3 &c, const double3 &d);
double orient3d_value(const float3 &a, const float3 &b, const float3 &c, const float3 &d);
/** 2^8 epsilon, epsilon = 2^-53. */
constexpr double orient3d_value_rel = 0x1.0p-45;

int orient2d(const float2 &a, const float2 &b, const float2 &c);
int orient3d(const float3 &a, const float3 &b, const float3 &c, const float3 &d);
int incircle(const float2 &a, const float2 &b, const float2 &c, const float2 &d);
int insphere(
    const float3 &a, const float3 &b, const float3 &c, const float3 &d, const float3 &e);

/* Diagnostics. */
enum class Predicate { Orient2d, Orient3d, Incircle, Insphere, Orient3dValue, Count };
/* A: static filter; B, C: adaptive stages; T: early exit because the input
 * differences were exact (zero tails); D: fully exact stage. */
enum class Stage { A, B, T, C, D, Count };

/** Per-stage hit counts; all zero unless linked against predicates_stats. */
uint64_t stage_hits(Predicate p, Stage s);
void reset_stage_hits();
bool stats_enabled();

/** Runs inside the predicates' translation units, so it sees their FP flags.
 * Returns false if multiply-add contraction or reassociation is detected.
 * On failure, *what (if non-null) names the failed probe. */
bool fp_selftest(const char **what = nullptr);

/* ---------------- exact integer predicates ---------------- */

namespace detail {
template <int Bits> inline void check_range(int64_t v)
{
#if LITESTL_PRED_CHECK_RANGE
  constexpr int64_t lim = int64_t(1) << Bits;
  if (v > lim || v < -lim) {
    fprintf(stderr, "litestl::math::pred: integer input %lld outside +-2^%d\n", (long long)v, Bits);
    abort();
  }
#else
  (void)v;
#endif
}

template <int Bits, int N> inline void check_range(const Vec<int64_t, N> &p)
{
  for (int i = 0; i < N; i++) {
    check_range<Bits>(p[i]);
  }
}

template <typename T> inline int sign_of(const T &v)
{
  if constexpr (std::is_integral_v<T>) {
    return v > 0 ? 1 : (v < 0 ? -1 : 0);
  }
  else {
    return v.sign();
  }
}

} // namespace detail

template <int Bits = 26> int orient2d(const int64_2 &a, const int64_2 &b, const int64_2 &c)
{
  static_assert(2 * Bits + 3 <= 63, "orient2d: Bits too large for int64 accumulation");
  detail::check_range<Bits>(a);
  detail::check_range<Bits>(b);
  detail::check_range<Bits>(c);
  const int64_t acx = a[0] - c[0], acy = a[1] - c[1];
  const int64_t bcx = b[0] - c[0], bcy = b[1] - c[1];
  return detail::sign_of(acx * bcy - acy * bcx);
}

namespace detail {
template <int Bits> Int128 orient3d_det(const int64_3 &a, const int64_3 &b, const int64_3 &c, const int64_3 &d)
{
  static_assert(3 * Bits + 7 <= 127, "orient3d: Bits too large for Int128");
  static_assert(2 * Bits + 3 <= 63, "orient3d: 2x2 minors must fit int64");
  check_range<Bits>(a);
  check_range<Bits>(b);
  check_range<Bits>(c);
  check_range<Bits>(d);
  const int64_t adx = a[0] - d[0], ady = a[1] - d[1], adz = a[2] - d[2];
  const int64_t bdx = b[0] - d[0], bdy = b[1] - d[1], bdz = b[2] - d[2];
  const int64_t cdx = c[0] - d[0], cdy = c[1] - d[1], cdz = c[2] - d[2];
  /* Shewchuk's orient3d = adz*(bdx*cdy - cdx*bdy) + bdz*(cdx*ady - adx*cdy)
   *                     + cdz*(adx*bdy - bdx*ady) */
  return mul_wide(adz, bdx * cdy - cdx * bdy) + mul_wide(bdz, cdx * ady - adx * cdy) +
         mul_wide(cdz, adx * bdy - bdx * ady);
}
} // namespace detail

template <int Bits = 26>
int orient3d(const int64_3 &a, const int64_3 &b, const int64_3 &c, const int64_3 &d)
{
  return detail::orient3d_det<Bits>(a, b, c, d).sign();
}

/** orient3d_value on the lattice: the exact determinant, converted with a
 * relative error of at most 3 epsilon. */
template <int Bits = 26>
double orient3d_value(const int64_3 &a, const int64_3 &b, const int64_3 &c, const int64_3 &d)
{
  return detail::orient3d_det<Bits>(a, b, c, d).to_double();
}

template <int Bits = 26>
int incircle(const int64_2 &a, const int64_2 &b, const int64_2 &c, const int64_2 &d)
{
  static_assert(4 * Bits + 9 <= 127, "incircle: Bits too large for Int128");
  detail::check_range<Bits>(a);
  detail::check_range<Bits>(b);
  detail::check_range<Bits>(c);
  detail::check_range<Bits>(d);
  const int64_t adx = a[0] - d[0], ady = a[1] - d[1];
  const int64_t bdx = b[0] - d[0], bdy = b[1] - d[1];
  const int64_t cdx = c[0] - d[0], cdy = c[1] - d[1];
  /* lifted terms (<= 2^(2B+3)) and 2x2 minors (<= 2^(2B+3)) fit int64 */
  const int64_t alift = adx * adx + ady * ady;
  const int64_t blift = bdx * bdx + bdy * bdy;
  const int64_t clift = cdx * cdx + cdy * cdy;
  const int64_t bc = bdx * cdy - cdx * bdy;
  const int64_t ca = cdx * ady - adx * cdy;
  const int64_t ab = adx * bdy - bdx * ady;
  const Int128 det = mul_wide(alift, bc) + mul_wide(blift, ca) + mul_wide(clift, ab);
  return det.sign();
}

template <int Bits = 26>
int insphere(const int64_3 &a,
             const int64_3 &b,
             const int64_3 &c,
             const int64_3 &d,
             const int64_3 &e)
{
  static_assert(5 * Bits + 12 <= 255, "insphere: Bits too large for Int256");
  static_assert(3 * Bits + 7 <= 127, "insphere: 3x3 minors must fit Int128");
  static_assert(2 * Bits + 4 <= 63, "insphere: lifted terms must fit int64");
  detail::check_range<Bits>(a);
  detail::check_range<Bits>(b);
  detail::check_range<Bits>(c);
  detail::check_range<Bits>(d);
  detail::check_range<Bits>(e);
  const int64_t aex = a[0] - e[0], aey = a[1] - e[1], aez = a[2] - e[2];
  const int64_t bex = b[0] - e[0], bey = b[1] - e[1], bez = b[2] - e[2];
  const int64_t cex = c[0] - e[0], cey = c[1] - e[1], cez = c[2] - e[2];
  const int64_t dex = d[0] - e[0], dey = d[1] - e[1], dez = d[2] - e[2];
  /* Same expansion as Shewchuk's inspherefast: 2x2 minors, then 3x3 minors
   * (Int128), then lifted cofactors (Int256). */
  const int64_t ab = aex * bey - bex * aey;
  const int64_t bc = bex * cey - cex * bey;
  const int64_t cd = cex * dey - dex * cey;
  const int64_t da = dex * aey - aex * dey;
  const int64_t ac = aex * cey - cex * aey;
  const int64_t bd = bex * dey - dex * bey;

  const Int128 abc = mul_wide(aez, bc) - mul_wide(bez, ac) + mul_wide(cez, ab);
  const Int128 bcd = mul_wide(bez, cd) - mul_wide(cez, bd) + mul_wide(dez, bc);
  const Int128 cda = mul_wide(cez, da) + mul_wide(dez, ac) + mul_wide(aez, cd);
  const Int128 dab = mul_wide(dez, ab) + mul_wide(aez, bd) + mul_wide(bez, da);

  /* lifts <= 3 * 2^(2B+2) fit int64; products with the 3x3 minors need Int256 */
  const Int128 alift = Int128::from(aex * aex + aey * aey + aez * aez);
  const Int128 blift = Int128::from(bex * bex + bey * bey + bez * bez);
  const Int128 clift = Int128::from(cex * cex + cey * cey + cez * cez);
  const Int128 dlift = Int128::from(dex * dex + dey * dey + dez * dez);
  const Int256 det = (mul_wide(dlift, abc) - mul_wide(clift, dab)) +
                     (mul_wide(blift, cda) - mul_wide(alift, bcd));
  return det.sign();
}

/* ---------------- symbolically perturbed incircle ---------------- */

namespace detail {
/* Sign of incircle(a,b,c,d) when it is exactly 0, with point i lifted by
 * eps^(rank_i + 1). The perturbed 4x4 determinant det[x y x^2+y^2 1] is
 * D0 + sum_i eps^(rank_i+1) * C_i with cofactors
 *   C_a = +orient2d(b,c,d)   C_b = -orient2d(a,c,d)
 *   C_c = +orient2d(a,b,d)   C_d = -orient2d(a,b,c)
 * so with D0 = 0 the sign is that of the first nonzero C_i in increasing rank
 * (a smaller rank is a larger perturbation). */
template <typename P, typename Orient>
int incircle_tie(const P &a, const P &b, const P &c, const P &d, const uint64_t (&rank)[4], Orient &&orient)
{
  int order[4] = {0, 1, 2, 3};
  for (int i = 1; i < 4; i++) {
    for (int j = i; j > 0 && rank[order[j]] < rank[order[j - 1]]; j--) {
      const int t = order[j];
      order[j] = order[j - 1];
      order[j - 1] = t;
    }
  }
  for (int k = 0; k < 4; k++) {
    int s = 0;
    switch (order[k]) {
      case 0:
        s = orient(b, c, d);
        break;
      case 1:
        s = -orient(a, c, d);
        break;
      case 2:
        s = orient(a, b, d);
        break;
      default:
        s = -orient(a, b, c);
        break;
    }
    if (s != 0) {
      return s;
    }
  }
  return 0;
}
} // namespace detail

/**
 * incircle with cocircular ties broken by Simulation of Simplicity: point i
 * is lifted to x^2 + y^2 + eps^(rank_i + 1). The result is nonzero whenever
 * the four points are distinct and not all collinear; it equals incircle()
 * whenever that is nonzero. Ranks must be distinct (e.g. lexicographic
 * order); then a Delaunay triangulation using this predicate is unique.
 */
inline int incircle_sos(const double2 &a,
                        const double2 &b,
                        const double2 &c,
                        const double2 &d,
                        uint64_t ra,
                        uint64_t rb,
                        uint64_t rc,
                        uint64_t rd)
{
  if (const int s = incircle(a, b, c, d)) {
    return s;
  }
  const uint64_t r[4] = {ra, rb, rc, rd};
  return detail::incircle_tie(a, b, c, d, r, [](const double2 &x, const double2 &y, const double2 &z) {
    return orient2d(x, y, z);
  });
}

inline int incircle_sos(const float2 &a,
                        const float2 &b,
                        const float2 &c,
                        const float2 &d,
                        uint64_t ra,
                        uint64_t rb,
                        uint64_t rc,
                        uint64_t rd)
{
  if (const int s = incircle(a, b, c, d)) {
    return s;
  }
  const uint64_t r[4] = {ra, rb, rc, rd};
  return detail::incircle_tie(
      a, b, c, d, r, [](const float2 &x, const float2 &y, const float2 &z) { return orient2d(x, y, z); });
}

template <int Bits = 26>
int incircle_sos(const int64_2 &a,
                 const int64_2 &b,
                 const int64_2 &c,
                 const int64_2 &d,
                 uint64_t ra,
                 uint64_t rb,
                 uint64_t rc,
                 uint64_t rd)
{
  if (const int s = incircle<Bits>(a, b, c, d)) {
    return s;
  }
  const uint64_t r[4] = {ra, rb, rc, rd};
  return detail::incircle_tie(a, b, c, d, r, [](const int64_2 &x, const int64_2 &y, const int64_2 &z) {
    return orient2d<Bits>(x, y, z);
  });
}

} // namespace litestl::math::pred
