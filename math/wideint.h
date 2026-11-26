#pragma once

/* Fixed-width signed two's-complement integers: Int128, Int256.
 *
 * Arithmetic wraps (like unsigned built-ins); callers size the type so that
 * the *final* result fits. Intermediate overflow is harmless under wrapping.
 * There is deliberately no division (predicates don't need it, and it avoids
 * __divti3 link dependencies on MSVC-ABI clang).
 *
 * Two implementations of the 64x64->128 product are always compiled:
 * detail::portable (32-bit halves) and, when available, unsigned __int128.
 * LITESTL_NO_INT128 forces the portable one everywhere. The struct layout
 * never depends on that choice. */

#include <compare>
#include <cstdint>
#include <type_traits>

#include "../util/type_tags.h"

#if defined(__SIZEOF_INT128__) && !defined(LITESTL_NO_INT128)
#define LITESTL_HAVE_INT128 1
#else
#define LITESTL_HAVE_INT128 0
#endif

namespace litestl::math {

namespace detail {
namespace portable {
/* Returns the low 64 bits of a*b and stores the high 64 bits in *hi. */
constexpr uint64_t umul64(uint64_t a, uint64_t b, uint64_t *hi)
{
  const uint64_t a0 = uint32_t(a), a1 = a >> 32;
  const uint64_t b0 = uint32_t(b), b1 = b >> 32;
  const uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
  const uint64_t mid = (p00 >> 32) + uint32_t(p01) + uint32_t(p10);
  *hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
  return (mid << 32) | uint32_t(p00);
}
} // namespace portable

#if LITESTL_HAVE_INT128
namespace fast {
constexpr uint64_t umul64(uint64_t a, uint64_t b, uint64_t *hi)
{
  const unsigned __int128 p = (unsigned __int128)a * b;
  *hi = uint64_t(p >> 64);
  return uint64_t(p);
}
} // namespace fast
#endif

constexpr uint64_t umul64(uint64_t a, uint64_t b, uint64_t *hi)
{
#if LITESTL_HAVE_INT128
  return fast::umul64(a, b, hi);
#else
  return portable::umul64(a, b, hi);
#endif
}
} // namespace detail

/** N little-endian 64-bit words, two's complement. */
template <int N> struct WideInt {
  static_assert(N >= 2);
  static constexpr int words = N;
  static constexpr int bits = 64 * N;

  uint64_t w[N] = {};

  static constexpr WideInt from(int64_t v)
  {
    WideInt r;
    r.w[0] = uint64_t(v);
    for (int i = 1; i < N; i++) {
      r.w[i] = v < 0 ? ~uint64_t(0) : 0;
    }
    return r;
  }

  /** Sign-extends a narrower WideInt. */
  template <int M> requires(M < N) static constexpr WideInt from(const WideInt<M> &v)
  {
    WideInt r;
    for (int i = 0; i < M; i++) {
      r.w[i] = v.w[i];
    }
    const uint64_t ext = v.is_negative() ? ~uint64_t(0) : 0;
    for (int i = M; i < N; i++) {
      r.w[i] = ext;
    }
    return r;
  }

  constexpr bool is_negative() const
  {
    return (w[N - 1] >> 63) != 0;
  }

  constexpr bool is_zero() const
  {
    for (int i = 0; i < N; i++) {
      if (w[i]) {
        return false;
      }
    }
    return true;
  }

  constexpr int sign() const
  {
    return is_negative() ? -1 : (is_zero() ? 0 : 1);
  }

  constexpr WideInt operator+(const WideInt &b) const
  {
    WideInt r;
    uint64_t carry = 0;
    for (int i = 0; i < N; i++) {
      const uint64_t s = w[i] + b.w[i];
      const uint64_t c1 = s < w[i];
      r.w[i] = s + carry;
      carry = c1 | (r.w[i] < s);
    }
    return r;
  }

  constexpr WideInt operator~() const
  {
    WideInt r;
    for (int i = 0; i < N; i++) {
      r.w[i] = ~w[i];
    }
    return r;
  }

  constexpr WideInt operator-() const
  {
    return ~*this + from(1);
  }

  constexpr WideInt operator-(const WideInt &b) const
  {
    WideInt r;
    uint64_t borrow = 0;
    for (int i = 0; i < N; i++) {
      const uint64_t d = w[i] - b.w[i];
      const uint64_t b1 = w[i] < b.w[i];
      r.w[i] = d - borrow;
      borrow = b1 | (d < borrow);
    }
    return r;
  }

  constexpr WideInt &operator+=(const WideInt &b)
  {
    return *this = *this + b;
  }

  constexpr WideInt &operator-=(const WideInt &b)
  {
    return *this = *this - b;
  }

  /** Wrapping N x N -> N word product. */
  constexpr WideInt operator*(const WideInt &b) const
  {
    WideInt r;
    for (int i = 0; i < N; i++) {
      uint64_t carry = 0;
      for (int j = 0; i + j < N; j++) {
        uint64_t hi;
        const uint64_t lo = detail::umul64(w[i], b.w[j], &hi);
        uint64_t s = r.w[i + j] + lo;
        hi += s < lo;
        s += carry;
        hi += s < carry;
        r.w[i + j] = s;
        carry = hi;
      }
    }
    return r;
  }

  /** Shift left by 0 <= n < bits. */
  constexpr WideInt operator<<(int n) const
  {
    WideInt r;
    const int ws = n / 64, bs = n % 64;
    for (int i = N - 1; i >= ws; i--) {
      uint64_t v = w[i - ws] << bs;
      if (bs && i - ws - 1 >= 0) {
        v |= w[i - ws - 1] >> (64 - bs);
      }
      r.w[i] = v;
    }
    return r;
  }

  /** Arithmetic shift right by 0 <= n < bits. */
  constexpr WideInt operator>>(int n) const
  {
    WideInt r;
    const uint64_t ext = is_negative() ? ~uint64_t(0) : 0;
    const int ws = n / 64, bs = n % 64;
    for (int i = 0; i < N; i++) {
      const int src = i + ws;
      uint64_t lo = src < N ? w[src] : ext;
      uint64_t hi = src + 1 < N ? w[src + 1] : ext;
      r.w[i] = bs ? (lo >> bs) | (hi << (64 - bs)) : lo;
    }
    return r;
  }

  constexpr bool operator==(const WideInt &b) const = default;

  constexpr std::strong_ordering operator<=>(const WideInt &b) const
  {
    const int64_t ah = int64_t(w[N - 1]), bh = int64_t(b.w[N - 1]);
    if (ah != bh) {
      return ah <=> bh;
    }
    for (int i = N - 2; i >= 0; i--) {
      if (w[i] != b.w[i]) {
        return w[i] <=> b.w[i];
      }
    }
    return std::strong_ordering::equal;
  }

  /** Approximate conversion (not correctly rounded). */
  constexpr double to_double() const
  {
    const bool neg = is_negative();
    const WideInt m = neg ? -*this : *this;
    double r = 0.0;
    for (int i = N - 1; i >= 0; i--) {
      r = r * 18446744073709551616.0 + double(m.w[i]);
    }
    return neg ? -r : r;
  }
};

using Int128 = WideInt<2>;
using Int256 = WideInt<4>;

namespace detail {
/* Unsigned N x N -> 2N word product (little-endian). */
template <int N>
constexpr void umul_words(const uint64_t *a, const uint64_t *b, uint64_t *out)
{
  for (int i = 0; i < 2 * N; i++) {
    out[i] = 0;
  }
  for (int i = 0; i < N; i++) {
    uint64_t carry = 0;
    for (int j = 0; j < N; j++) {
      uint64_t hi;
      const uint64_t lo = umul64(a[i], b[j], &hi);
      uint64_t s = out[i + j] + lo;
      hi += s < lo;
      s += carry;
      hi += s < carry;
      out[i + j] = s;
      carry = hi;
    }
    out[i + N] = carry;
  }
}
} // namespace detail

/** Exact signed product of two int64 values. */
constexpr Int128 mul_wide(int64_t a, int64_t b)
{
  uint64_t hi;
  const uint64_t lo = detail::umul64(uint64_t(a), uint64_t(b), &hi);
  /* unsigned -> signed correction */
  if (a < 0) {
    hi -= uint64_t(b);
  }
  if (b < 0) {
    hi -= uint64_t(a);
  }
  Int128 r;
  r.w[0] = lo;
  r.w[1] = hi;
  return r;
}

/** Exact signed product of two WideInt<N> values. */
template <int N> constexpr WideInt<2 * N> mul_wide(const WideInt<N> &a, const WideInt<N> &b)
{
  WideInt<2 * N> r;
  detail::umul_words<N>(a.w, b.w, r.w);
  /* unsigned -> signed correction: subtract (b << 64N) if a < 0, etc. */
  if (a.is_negative()) {
    WideInt<2 * N> t;
    for (int i = 0; i < N; i++) {
      t.w[N + i] = b.w[i];
    }
    r -= t;
  }
  if (b.is_negative()) {
    WideInt<2 * N> t;
    for (int i = 0; i < N; i++) {
      t.w[N + i] = a.w[i];
    }
    r -= t;
  }
  return r;
}

} // namespace litestl::math

force_type_is_simple(litestl::math::Int128);
force_type_is_simple(litestl::math::Int256);
