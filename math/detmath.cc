/* Deterministic elementary functions (detmath.h).
 *
 * Built with strict floating-point flags (see math/CMakeLists.txt). The
 * constants were generated exactly (rational arithmetic) by a script kept in
 * the UberTopo P4a plan's work notes; the Cody-Waite splits leave enough zero
 * low bits that k * c_1 and k * c_2 are exact for the k each reduction allows.
 *
 * Kernels are Taylor series, truncated where the remainder is below 2^-60
 * relative on the reduced interval, so no third-party minimax coefficients
 * are involved. */

#include "detmath.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace litestl::math::detmath {

namespace {

/* pi/2 = kPio2_1 + kPio2_2 + kPio2_3 (+ 1.0e-37); the first two have 33
 * significant bits, so k * kPio2_{1,2} is exact for |k| < 2^20. */
constexpr double kPio2_1 = 0x1.921fb54400000p+0;
constexpr double kPio2_2 = 0x1.0b4611a600000p-34;
constexpr double kPio2_3 = 0x1.3198a2e037073p-69;
constexpr double kTwoPi_1 = 0x1.921fb54400000p+2;
constexpr double kTwoPi_2 = 0x1.0b4611a600000p-32;
constexpr double kTwoPi_3 = 0x1.3198a2e037073p-67;
/* ln 2 = kLn2_1 + kLn2_2 (+ 1.9e-31); kLn2_1 has 42 bits, so k * kLn2_1 is
 * exact for |k| < 2^11 */
constexpr double kLn2_1 = 0x1.62e42fefa3800p-1;
constexpr double kLn2_2 = 0x1.ef35793c76730p-45;
constexpr double kTwoOverPi = 0x1.45f306dc9c883p-1;
constexpr double kInvLn2 = 0x1.71547652b82fep+0;
constexpr double kInvTwoPi = 0x1.45f306dc9c883p-3;
constexpr double kPi = 0x1.921fb54442d18p+1;
constexpr double kPio4 = 0x1.921fb54442d18p-1;
constexpr double kSqrtHalf = 0x1.6a09e667f3bcdp-1;
constexpr double kLn2 = 0x1.62e42fefa39efp-1;
/* 1/n!, correctly rounded */
constexpr double kInvFact[24] = {
    0x1.0000000000000p+0,  0x1.0000000000000p+0,  0x1.0000000000000p-1,  0x1.5555555555555p-3,
    0x1.5555555555555p-5,  0x1.1111111111111p-7,  0x1.6c16c16c16c17p-10, 0x1.a01a01a01a01ap-13,
    0x1.a01a01a01a01ap-16, 0x1.71de3a556c734p-19, 0x1.27e4fb7789f5cp-22, 0x1.ae64567f544e4p-26,
    0x1.1eed8eff8d898p-29, 0x1.6124613a86d09p-33, 0x1.93974a8c07c9dp-37, 0x1.ae7f3e733b81fp-41,
    0x1.ae7f3e733b81fp-45, 0x1.952c77030ad4ap-49, 0x1.6827863b97d97p-53, 0x1.2f49b46814157p-57,
    0x1.e542ba4020225p-62, 0x1.71b8ef6dcf572p-66, 0x1.0ce396db7f853p-70, 0x1.761b41316381ap-75};
/* 1/(2j+1), correctly rounded */
constexpr double kInvOdd[14] = {0x1.0000000000000p+0, 0x1.5555555555555p-2, 0x1.999999999999ap-3,
                                0x1.2492492492492p-3, 0x1.c71c71c71c71cp-4, 0x1.745d1745d1746p-4,
                                0x1.3b13b13b13b14p-4, 0x1.1111111111111p-4, 0x1.e1e1e1e1e1e1ep-5,
                                0x1.af286bca1af28p-5, 0x1.8618618618618p-5, 0x1.642c8590b2164p-5,
                                0x1.47ae147ae147bp-5, 0x1.2f684bda12f68p-5};

constexpr double kExpOverflow = 0x1.62e42fefa39efp+9;   /* ln(DBL_MAX) ~ 709.78 */
constexpr double kExpUnderflow = -0x1.74910d52d3051p+9; /* ln(2^-1075) ~ -745.13 */
constexpr double kCoshOverflow = 0x1.633ce8fb9f87dp+9;  /* ~ 710.4758 */

inline double quiet_nan()
{
  return std::numeric_limits<double>::quiet_NaN();
}

/* Nearest integer, ties to even, for |x| < 2^51 (default rounding mode). */
inline double round_int(double x)
{
  constexpr double big = 0x1.8p52;
  return x >= 0 ? (x + big) - big : (x - big) + big;
}

inline void two_sum(double a, double b, double &s, double &e)
{
  s = a + b;
  const double bb = s - a;
  e = (a - (s - bb)) + (b - bb);
}

/* sin(x + y) for |x| <= ~pi/4 + eps, |y| <= ulp(x) */
inline double ksin(double x, double y)
{
  const double z = x * x;
  /* sin x = x + x z (-1/3! + z (1/5! - z (1/7! ...))), through z^8 / 17! */
  double p = kInvFact[17];
  p = -kInvFact[15] + z * p;
  p = kInvFact[13] + z * p;
  p = -kInvFact[11] + z * p;
  p = kInvFact[9] + z * p;
  p = -kInvFact[7] + z * p;
  p = kInvFact[5] + z * p;
  p = -kInvFact[3] + z * p;
  return x + ((x * z) * p + y);
}

/* cos(x + y) for |x| <= ~pi/4 + eps, |y| <= ulp(x) */
inline double kcos(double x, double y)
{
  const double z = x * x;
  /* cos x = 1 - z/2 + z^2 (1/4! - z (1/6! ...)), through z^9 / 18! */
  double q = -kInvFact[18];
  q = kInvFact[16] + z * q;
  q = -kInvFact[14] + z * q;
  q = kInvFact[12] + z * q;
  q = -kInvFact[10] + z * q;
  q = kInvFact[8] + z * q;
  q = -kInvFact[6] + z * q;
  q = kInvFact[4] + z * q;
  const double hz = 0.5 * z;
  const double w = 1.0 - hz;
  /* (1 - w) - hz is the exact rounding error of w */
  return w + (((1.0 - w) - hz) + ((z * z) * q - x * y));
}

/* x = k pi/2 + (rh + rl); returns k mod 4. |x| <= kMaxTrigArg. */
inline int reduce_pio2(double x, double &rh, double &rl)
{
  const double k = round_int(x * kTwoOverPi);
  const double r1 = x - k * kPio2_1; /* exact (Sterbenz) */
  double s, e1, s2, e2;
  two_sum(r1, -(k * kPio2_2), s, e1); /* the product is exact */
  two_sum(s, -(k * kPio2_3), s2, e2);
  two_sum(s2, e1 + e2, rh, rl);
  return int(int64_t(k) & 3);
}

} // namespace

double ulp(double x)
{
  if (!std::isfinite(x)) {
    return quiet_nan();
  }
  x = std::fabs(x);
  if (x < 0x1p-1022) {
    return 0x1p-1074;
  }
  return std::ldexp(1.0, std::ilogb(x) - 52);
}

void sincos(double x, double *s, double *c)
{
  const double a = std::fabs(x);
  if (!(a <= kMaxTrigArg)) {
    *s = *c = quiet_nan();
    return;
  }
  if (a < 0x1p-27) {
    /* sin x = x and cos x = 1 to within 2^-55 relative; keeps sin(-0) = -0 */
    *s = x;
    *c = 1.0;
    return;
  }
  if (a <= kPio4) {
    *s = ksin(x, 0.0);
    *c = kcos(x, 0.0);
    return;
  }
  double rh, rl;
  const int n = reduce_pio2(x, rh, rl);
  const double ss = ksin(rh, rl), cc = kcos(rh, rl);
  switch (n) {
    case 0:
      *s = ss;
      *c = cc;
      break;
    case 1:
      *s = cc;
      *c = -ss;
      break;
    case 2:
      *s = -ss;
      *c = -cc;
      break;
    default:
      *s = -cc;
      *c = ss;
      break;
  }
}

double sin(double x)
{
  double s, c;
  sincos(x, &s, &c);
  return s;
}

double cos(double x)
{
  double s, c;
  sincos(x, &s, &c);
  return c;
}

double reduce_angle(double x)
{
  if (!(std::fabs(x) <= kMaxTrigArg)) {
    return quiet_nan();
  }
  const double k = round_int(x * kInvTwoPi);
  if (k == 0) {
    return x == -kPi ? kPi : x;
  }
  const double r1 = x - k * kTwoPi_1;
  double s, e1, s2, e2;
  two_sum(r1, -(k * kTwoPi_2), s, e1);
  two_sum(s, -(k * kTwoPi_3), s2, e2);
  double r = s2 + (e1 + e2);
  if (r <= -kPi) {
    r += 2 * kPi;
  }
  else if (r > kPi) {
    r -= 2 * kPi;
  }
  return r;
}

double exp(double x)
{
  if (x != x) {
    return x;
  }
  if (x > kExpOverflow) {
    return std::numeric_limits<double>::infinity();
  }
  if (x < kExpUnderflow) {
    return 0.0;
  }
  const double k = round_int(x * kInvLn2);
  const double hi = x - k * kLn2_1; /* exact */
  const double lo = -(k * kLn2_2);
  const double r = hi + lo;
  const double rl = (hi - r) + lo;
  /* exp r = 1 + r + r^2 (1/2! + r (1/3! + ...)), through r^13 / 13! */
  double p = kInvFact[13];
  for (int n = 12; n >= 2; n--) {
    p = kInvFact[n] + r * p;
  }
  const double e = 1.0 + (r + ((r * r) * p + rl * (1.0 + r)));
  return std::ldexp(e, int(k));
}

namespace {

/* log(1 + f) for f in [sqrt(1/2) - 1, sqrt(2) - 1], f exact */
inline double klog1p(double f, double ek)
{
  const double s = f / (2.0 + f);
  const double z = s * s;
  /* R = 2 z (1/3 + z (1/5 + ...)), through z^11 */
  double r = kInvOdd[11];
  for (int j = 10; j >= 1; j--) {
    r = kInvOdd[j] + z * r;
  }
  r = 2.0 * z * r;
  const double hfsq = 0.5 * f * f;
  /* log(1 + f) = f - (hfsq - s (hfsq + R)) */
  if (ek == 0) {
    return f - (hfsq - s * (hfsq + r));
  }
  return ek * kLn2_1 - ((hfsq - (s * (hfsq + r) + ek * kLn2_2)) - f);
}

} // namespace

double log(double x)
{
  if (x != x || x < 0) {
    return quiet_nan();
  }
  if (x == 0) {
    return -std::numeric_limits<double>::infinity();
  }
  if (std::isinf(x)) {
    return x;
  }
  int e;
  double m = std::frexp(x, &e); /* [0.5, 1) */
  if (m < kSqrtHalf) {
    m *= 2;
    e -= 1;
  }
  return klog1p(m - 1.0, double(e)); /* m - 1 is exact */
}

double log1p(double y)
{
  if (y != y || y < -1) {
    return quiet_nan();
  }
  if (y == -1) {
    return -std::numeric_limits<double>::infinity();
  }
  if (std::isinf(y)) {
    return y;
  }
  if (y >= kSqrtHalf - 1.0 && y <= 0x1.6a09e667f3bcdp+0 - 1.0) {
    return klog1p(y, 0.0);
  }
  double u, err;
  two_sum(1.0, y, u, err);
  return log(u) + err / u;
}

double cosh(double x)
{
  const double a = std::fabs(x);
  if (a != a) {
    return a;
  }
  if (a > kCoshOverflow) {
    return std::numeric_limits<double>::infinity();
  }
  if (a > 709.0) {
    const double e = exp(0.5 * a);
    return (0.5 * e) * e;
  }
  const double e = exp(a);
  return 0.5 * (e + 1.0 / e);
}

double sinh(double x)
{
  const double a = std::fabs(x);
  if (a != a) {
    return x;
  }
  double r;
  if (a < 1.0) {
    /* a + a z (1/3! + z (1/5! + ...)), through z^9 / 19! */
    const double z = a * a;
    double p = kInvFact[19];
    for (int n = 17; n >= 3; n -= 2) {
      p = kInvFact[n] + z * p;
    }
    r = a + (a * z) * p;
  }
  else if (a <= 709.0) {
    const double e = exp(a);
    r = 0.5 * (e - 1.0 / e);
  }
  else if (a <= kCoshOverflow) {
    const double e = exp(0.5 * a);
    r = (0.5 * e) * e;
  }
  else {
    r = std::numeric_limits<double>::infinity();
  }
  return std::copysign(r, x);
}

double asinh(double x)
{
  const double a = std::fabs(x);
  if (a != a || std::isinf(a)) {
    return x;
  }
  double r;
  if (a > 0x1p28) {
    r = log(a) + kLn2;
  }
  else if (a > 2.0) {
    r = log(2.0 * a + 1.0 / (std::sqrt(a * a + 1.0) + a));
  }
  else {
    const double t = a * a;
    r = log1p(a + t / (1.0 + std::sqrt(1.0 + t)));
  }
  return std::copysign(r, x);
}

} // namespace litestl::math::detmath
