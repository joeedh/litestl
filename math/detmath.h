#pragma once

/* Deterministic elementary functions.
 *
 * The platform libm differs between glibc, the MSVC runtime and others, and
 * between versions of each, so results that feed decisions (geometry,
 * hashes, goldens) must not depend on it. These functions use only IEEE
 * double arithmetic (+ - * /, sqrt, exact scaling) with fixed argument
 * reductions and fixed polynomials, in a translation unit built with strict
 * floating-point flags (no FMA contraction, no fast-math). They therefore give
 * the same bits under every conforming compiler and platform.
 *
 * Accuracy is stated as bounds that callers may rely on in error budgets
 * (they are checked against a quad-precision oracle in test_detmath):
 *  - relative bounds are in units of ulp(exact result);
 *  - sin and cos past pi/4 have an absolute bound instead, because a result
 *    near a zero of sin or cos inherits the reduction's absolute error.
 *
 * Domains: sin, cos, sincos and reduce_angle need |x| <= kMaxTrigArg and
 * return NaN beyond it. exp, log, sinh, cosh and asinh accept every double
 * and follow the usual IEEE conventions for NaN, infinities and overflow.
 * Results in the subnormal range lose their relative bound (absolute error
 * stays below 2^-1074 times a small factor). */

#include <cstdint>

namespace litestl::math::detmath {

/** Largest |x| accepted by the trigonometric functions (about 1.57e6). */
inline constexpr double kMaxTrigArg = 0x1.8p20;

/* Error bounds (see the file comment). */
inline constexpr double kSinCosUlp = 2;       /* relative, |x| <= pi/4 */
inline constexpr double kSinCosAbs = 0x1p-52; /* absolute, |x| <= kMaxTrigArg */
inline constexpr double kExpUlp = 2;
inline constexpr double kLogUlp = 3;
inline constexpr double kLog1pUlp = 4; /* for y >= -0.29 */
inline constexpr double kSinhCoshUlp = 5;
inline constexpr double kAsinhUlp = 6;
inline constexpr double kReduceAngleAbs = 0x1p-50; /* absolute, |x| <= kMaxTrigArg */

double sin(double x);
double cos(double x);
void sincos(double x, double *s, double *c);
double exp(double x);
double log(double x);
double log1p(double y);
double sinh(double x);
double cosh(double x);
double asinh(double x);

/** x reduced to (-pi, pi] (x - 2 pi k for the nearest integer k). */
double reduce_angle(double x);

/** The spacing of doubles at |x|: 2^(exponent(x) - 52); 2^-1074 for zero and
 * subnormals; NaN for non-finite x. */
double ulp(double x);

} // namespace litestl::math::detmath
