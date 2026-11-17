#pragma once

/* Test-only arbitrary-precision signed integer, used as an independent oracle
 * for math/wideint.h and math/predicates. Deliberately a different
 * representation (sign-magnitude, 32-bit limbs, std::vector) and algorithm
 * (schoolbook) from the code under test. Not fast; not for production. */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace litestl::test {

class BigInt {
public:
  BigInt() = default;

  static BigInt from_u64(uint64_t v)
  {
    BigInt r;
    while (v) {
      r.mag_.push_back(uint32_t(v));
      v >>= 32;
    }
    return r;
  }

  static BigInt from_i64(int64_t v)
  {
    BigInt r = from_u64(v < 0 ? uint64_t(0) - uint64_t(v) : uint64_t(v));
    r.neg_ = v < 0 && !r.mag_.empty();
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
      r.mag_.push_back(uint32_t(t[i]));
      r.mag_.push_back(uint32_t(t[i] >> 32));
    }
    r.trim();
    r.neg_ = negative && !r.mag_.empty();
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
    r.neg_ = x < 0 && !r.mag_.empty();
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
    return mag_.empty() ? 0 : (neg_ ? -1 : 1);
  }

  bool operator==(const BigInt &b) const
  {
    return neg_ == b.neg_ && mag_ == b.mag_;
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
    r.neg_ = !r.neg_ && !r.mag_.empty();
    return r;
  }

  BigInt operator+(const BigInt &b) const
  {
    BigInt r;
    if (neg_ == b.neg_) {
      r.mag_ = add_mag(mag_, b.mag_);
      r.neg_ = neg_;
    }
    else if (cmp_mag(mag_, b.mag_) >= 0) {
      r.mag_ = sub_mag(mag_, b.mag_);
      r.neg_ = neg_;
    }
    else {
      r.mag_ = sub_mag(b.mag_, mag_);
      r.neg_ = b.neg_;
    }
    r.trim();
    if (r.mag_.empty()) {
      r.neg_ = false;
    }
    return r;
  }

  BigInt operator-(const BigInt &b) const
  {
    return *this + (-b);
  }

  BigInt operator*(const BigInt &b) const
  {
    BigInt r;
    if (mag_.empty() || b.mag_.empty()) {
      return r;
    }
    r.mag_.assign(mag_.size() + b.mag_.size(), 0);
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
    r.neg_ = (neg_ != b.neg_) && !r.mag_.empty();
    return r;
  }

  BigInt shl(int n) const
  {
    if (mag_.empty() || n == 0) {
      return *this;
    }
    BigInt r;
    r.neg_ = neg_;
    int words = n / 32, bits = n % 32;
    r.mag_.assign(words, 0);
    uint32_t carry = 0;
    for (uint32_t w : mag_) {
      r.mag_.push_back(bits ? (w << bits) | carry : w);
      carry = bits ? w >> (32 - bits) : 0;
    }
    if (carry) {
      r.mag_.push_back(carry);
    }
    return r;
  }

  std::string hex() const
  {
    if (mag_.empty()) {
      return "0x0";
    }
    std::string s = neg_ ? "-0x" : "0x";
    char buf[16];
    snprintf(buf, sizeof(buf), "%x", mag_.back());
    s += buf;
    for (size_t i = mag_.size() - 1; i-- > 0;) {
      snprintf(buf, sizeof(buf), "%08x", mag_[i]);
      s += buf;
    }
    return s;
  }

  /* Determinant by Laplace expansion (exact; fine for n <= 5). */
  static BigInt det(const std::vector<std::vector<BigInt>> &m)
  {
    size_t n = m.size();
    if (n == 1) {
      return m[0][0];
    }
    BigInt sum;
    for (size_t col = 0; col < n; col++) {
      if (m[0][col].sign() == 0) {
        continue;
      }
      std::vector<std::vector<BigInt>> minor;
      for (size_t r = 1; r < n; r++) {
        std::vector<BigInt> row;
        for (size_t c = 0; c < n; c++) {
          if (c != col) {
            row.push_back(m[r][c]);
          }
        }
        minor.push_back(row);
      }
      BigInt term = m[0][col] * det(minor);
      sum = (col & 1) ? sum - term : sum + term;
    }
    return sum;
  }

private:
  bool neg_ = false;
  std::vector<uint32_t> mag_; /* little-endian, no high zero limbs */

  void trim()
  {
    while (!mag_.empty() && mag_.back() == 0) {
      mag_.pop_back();
    }
  }

  static int cmp_mag(const std::vector<uint32_t> &a, const std::vector<uint32_t> &b)
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

  static std::vector<uint32_t> add_mag(const std::vector<uint32_t> &a,
                                       const std::vector<uint32_t> &b)
  {
    std::vector<uint32_t> r;
    uint64_t carry = 0;
    for (size_t i = 0; i < std::max(a.size(), b.size()); i++) {
      uint64_t t = carry + (i < a.size() ? a[i] : 0) + (i < b.size() ? b[i] : 0);
      r.push_back(uint32_t(t));
      carry = t >> 32;
    }
    if (carry) {
      r.push_back(uint32_t(carry));
    }
    return r;
  }

  /* requires |a| >= |b| */
  static std::vector<uint32_t> sub_mag(const std::vector<uint32_t> &a,
                                       const std::vector<uint32_t> &b)
  {
    std::vector<uint32_t> r;
    int64_t borrow = 0;
    for (size_t i = 0; i < a.size(); i++) {
      int64_t t = int64_t(a[i]) - (i < b.size() ? b[i] : 0) - borrow;
      borrow = t < 0;
      r.push_back(uint32_t(t + (borrow << 32)));
    }
    return r;
  }
};

} // namespace litestl::test
