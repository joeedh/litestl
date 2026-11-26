#pragma once

/* Test-only arbitrary-precision signed integer, used as an independent oracle
 * for math/wideint.h and math/predicates. Deliberately a different
 * representation (sign-magnitude, 32-bit limbs, std::vector) and algorithm
 * (schoolbook) from the code under test. Not fast; not for production. */

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../util/vector.h"

namespace litestl::test {

class BigInt {
  /* Inline capacity covers the 2-5 limb magnitudes of nearly every oracle value, so the
   * hot path (Laplace/DP determinants over scaled doubles) never touches the heap. */
  using Limbs = util::Vector<uint32_t, 16>;

public:
  BigInt() = default;

  static BigInt from_u64(uint64_t v)
  {
    BigInt r;
    while (v) {
      r.mag_.append(uint32_t(v));
      v >>= 32;
    }
    return r;
  }

  static BigInt from_i64(int64_t v)
  {
    BigInt r = from_u64(v < 0 ? uint64_t(0) - uint64_t(v) : uint64_t(v));
    r.neg_ = v < 0 && !r.mag_.isEmpty();
    return r;
  }

  /* Interprets n little-endian 64-bit words as a two's complement integer. */
  static BigInt from_twos(const uint64_t *w, int n)
  {
    bool negative = (w[n - 1] >> 63) & 1;
    std::vector<uint64_t> t(w, w + n);
    if (negative) {
      for (auto &x : t) {
        x = ~x;
      }
      for (int i = 0; i < n; i++) {
        if (++t[i] != 0) {
          break;
        }
      }
    }
    BigInt r;
    for (int i = 0; i < n; i++) {
      r.mag_.append(uint32_t(t[i]));
      r.mag_.append(uint32_t(t[i] >> 32));
    }
    r.trim();
    r.neg_ = negative && !r.mag_.isEmpty();
    return r;
  }

  /* Low n words of the two's complement representation (i.e. the value
   * reduced mod 2^(64n)), for comparing against wrapping arithmetic. */
  std::vector<uint64_t> to_twos(int n) const
  {
    std::vector<uint64_t> t(n, 0);
    for (size_t i = 0; i < mag_.size() && i / 2 < size_t(n); i++) {
      t[i / 2] |= uint64_t(mag_[i]) << (32 * (i & 1));
    }
    if (neg_) {
      for (auto &x : t) {
        x = ~x;
      }
      for (int i = 0; i < n; i++) {
        if (++t[i] != 0) {
          break;
        }
      }
    }
    return t;
  }

  /* Exact value of x * 2^k; x * 2^k must be an integer. */
  static BigInt from_double_scaled(double x, int k)
  {
    if (x == 0.0) {
      return BigInt();
    }
    int e;
    double m = std::frexp(std::fabs(x), &e); /* |x| = m * 2^e, m in [0.5, 1) */
    uint64_t mi = uint64_t(std::ldexp(m, 53)); /* exact: 53-bit integer */
    int shift = e - 53 + k;
    BigInt r = from_u64(mi);
    if (shift >= 0) {
      r = r.shl(shift);
    }
    else {
      /* must divide exactly */
      uint64_t mask = (uint64_t(1) << -shift) - 1;
      if (-shift >= 64 || (mi & mask)) {
        fprintf(stderr, "from_double_scaled: %a * 2^%d is not an integer\n", x, k);
        abort();
      }
      r = from_u64(mi >> -shift);
    }
    r.neg_ = x < 0 && !r.mag_.isEmpty();
    return r;
  }

  /* Exponent of the least significant mantissa unit: x is a multiple of
   * 2^unit_exponent(x). */
  static int unit_exponent(double x)
  {
    int e;
    std::frexp(x, &e);
    return e - 53;
  }

  int sign() const
  {
    return mag_.isEmpty() ? 0 : (neg_ ? -1 : 1);
  }

  bool operator==(const BigInt &b) const
  {
    if (neg_ != b.neg_ || mag_.size() != b.mag_.size()) {
      return false;
    }
    return std::equal(mag_.data(), mag_.data() + mag_.size(), b.mag_.data());
  }

  int cmp(const BigInt &b) const
  {
    if (neg_ != b.neg_) {
      return neg_ ? -1 : 1;
    }
    int c = cmp_mag(mag_, b.mag_);
    return neg_ ? -c : c;
  }

  BigInt operator-() const
  {
    BigInt r = *this;
    r.neg_ = !r.neg_ && !r.mag_.isEmpty();
    return r;
  }

  BigInt &operator+=(const BigInt &b)
  {
    add_signed(b, false);
    return *this;
  }

  BigInt &operator-=(const BigInt &b)
  {
    add_signed(b, true);
    return *this;
  }

  BigInt operator+(const BigInt &b) const
  {
    BigInt r = *this;
    r.add_signed(b, false);
    return r;
  }

  BigInt operator-(const BigInt &b) const
  {
    BigInt r = *this;
    r.add_signed(b, true);
    return r;
  }

  BigInt operator*(const BigInt &b) const
  {
    BigInt r;
    if (mag_.isEmpty() || b.mag_.isEmpty()) {
      return r;
    }
    r.mag_.resize(mag_.size() + b.mag_.size());
    for (size_t i = 0; i < mag_.size(); i++) {
      uint64_t carry = 0;
      for (size_t j = 0; j < b.mag_.size(); j++) {
        uint64_t t = uint64_t(mag_[i]) * b.mag_[j] + r.mag_[i + j] + carry;
        r.mag_[i + j] = uint32_t(t);
        carry = t >> 32;
      }
      size_t k = i + b.mag_.size();
      while (carry) {
        uint64_t t = uint64_t(r.mag_[k]) + carry;
        r.mag_[k++] = uint32_t(t);
        carry = t >> 32;
      }
    }
    r.trim();
    r.neg_ = (neg_ != b.neg_) && !r.mag_.isEmpty();
    return r;
  }

  BigInt shl(int n) const
  {
    if (mag_.isEmpty() || n == 0) {
      return *this;
    }
    BigInt r;
    r.neg_ = neg_;
    int words = n / 32, bits = n % 32;
    r.mag_.ensure_capacity(words + mag_.size() + 1);
    r.mag_.resize(words);
    uint32_t carry = 0;
    for (uint32_t w : mag_) {
      r.mag_.append(bits ? (w << bits) | carry : w);
      carry = bits ? w >> (32 - bits) : 0;
    }
    if (carry) {
      r.mag_.append(carry);
    }
    return r;
  }

  std::string hex() const
  {
    if (mag_.isEmpty()) {
      return "0x0";
    }
    std::string s = neg_ ? "-0x" : "0x";
    char buf[16];
    snprintf(buf, sizeof(buf), "%x", mag_[int(mag_.size()) - 1]);
    s += buf;
    for (size_t i = mag_.size() - 1; i-- > 0;) {
      snprintf(buf, sizeof(buf), "%08x", mag_[i]);
      s += buf;
    }
    return s;
  }

  /* Exact determinant by Laplace expansion along rows, bottom-up over column subsets:
   * minor[mask] is the determinant of the rows n-|mask|..n-1 restricted to the columns in
   * mask, so each minor is computed once (n*2^(n-1) multiplies) and nothing is copied. */
  static BigInt det(const std::vector<std::vector<BigInt>> &m)
  {
    const size_t n = m.size();
    if (n == 1) {
      return m[0][0];
    }
    std::vector<BigInt> minor(size_t(1) << n);
    minor[0] = from_u64(1);
    for (size_t mask = 1; mask < minor.size(); mask++) {
      const size_t k = size_t(std::popcount(mask));
      const std::vector<BigInt> &row = m[n - k];
      BigInt sum;
      size_t rank = 0;
      for (size_t col = 0; col < n; col++) {
        if (!(mask >> col & 1)) {
          continue;
        }
        const BigInt &e = row[col];
        if (e.sign() != 0) {
          sum.add_signed(e * minor[mask & ~(size_t(1) << col)], rank & 1);
        }
        rank++;
      }
      minor[mask] = std::move(sum);
    }
    return std::move(minor.back());
  }

private:
  bool neg_ = false;
  Limbs mag_; /* little-endian, no high zero limbs */

  void trim()
  {
    while (!mag_.isEmpty() && mag_[int(mag_.size()) - 1] == 0) {
      mag_.pop_back();
    }
  }

  static int cmp_mag(const Limbs &a, const Limbs &b)
  {
    if (a.size() != b.size()) {
      return a.size() < b.size() ? -1 : 1;
    }
    for (size_t i = a.size(); i-- > 0;) {
      if (a[i] != b[i]) {
        return a[i] < b[i] ? -1 : 1;
      }
    }
    return 0;
  }

  /* this += (negate ? -b : b), in place on the limbs. */
  void add_signed(const BigInt &b, bool negate)
  {
    if (b.mag_.isEmpty()) {
      return;
    }
    const bool bneg = b.neg_ != negate;
    if (mag_.isEmpty()) {
      mag_ = b.mag_;
      neg_ = bneg;
      return;
    }
    if (neg_ == bneg) {
      const size_t n = std::max(mag_.size(), b.mag_.size());
      mag_.resize(n + 1);
      uint64_t carry = 0;
      for (size_t i = 0; i < n; i++) {
        uint64_t t = carry + mag_[int(i)] + (i < b.mag_.size() ? b.mag_[int(i)] : 0);
        mag_[int(i)] = uint32_t(t);
        carry = t >> 32;
      }
      mag_[int(n)] = uint32_t(carry);
    }
    else if (cmp_mag(mag_, b.mag_) >= 0) {
      sub_limbs(mag_, b.mag_, mag_.size(), false);
    }
    else {
      mag_.resize(b.mag_.size());
      sub_limbs(mag_, b.mag_, b.mag_.size(), true);
      neg_ = bneg;
    }
    trim();
    if (mag_.isEmpty()) {
      neg_ = false;
    }
  }

  /* a = |a| - |b| (or |b| - |a| when reverse) over the first n limbs; the minuend
   * must be the larger magnitude. */
  static void sub_limbs(Limbs &a, const Limbs &b, size_t n, bool reverse)
  {
    int64_t borrow = 0;
    for (size_t i = 0; i < n; i++) {
      const int64_t x = i < a.size() ? a[int(i)] : 0;
      const int64_t y = i < b.size() ? b[int(i)] : 0;
      const int64_t t = (reverse ? y - x : x - y) - borrow;
      borrow = t < 0;
      a[int(i)] = uint32_t(t + (borrow << 32));
    }
  }
};

} // namespace litestl::test
