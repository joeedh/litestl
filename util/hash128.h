#pragma once

/* Stable 128-bit hashing: SipHash-2-4 with 128-bit output, as defined by the
 * reference implementation (https://github.com/veorq/SipHash; the 128-bit
 * variant is not in the 2012 paper), plus a canonical, type-tagged tuple
 * encoding for deriving persistent IDs and content hashes.
 *
 * - Every Hasher128 starts with a DomainTag, so different kinds of hashes
 *   (e.g. generator IDs vs operator-derived IDs vs content hashes) never share
 *   a space.
 * - Every typed encoder writes a 1-byte type tag then a little-endian payload,
 *   so tuples cannot collide structurally (u32,u32 vs u64).
 * - f64_canonical (-0 -> +0, NaN -> 0x7ff8000000000000) is for identity;
 *   f64_bits / raw_bytes are for content hashes where -0 and +0 differ.
 *
 * The default key is public: no adversarial collision resistance is claimed.
 * Outputs are part of the determinism contract; changing the encoding changes
 * every derived ID. */

#include <bit>
#include <compare>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "string.h"

namespace litestl::hash {

static_assert(std::endian::native == std::endian::little,
              "litestl assumes a little-endian host");

struct Hash128 {
  uint64_t lo = 0, hi = 0;

  constexpr bool operator==(const Hash128 &b) const = default;
  constexpr std::strong_ordering operator<=>(const Hash128 &b) const
  {
    if (hi != b.hi) {
      return hi <=> b.hi;
    }
    return lo <=> b.lo;
  }

  constexpr uint64_t computeHash() const
  {
    return lo ^ (hi * 0x9e3779b97f4a7c15ULL);
  }

  void to_le_bytes(uint8_t out[16]) const
  {
    for (int i = 0; i < 8; i++) {
      out[i] = uint8_t(lo >> (8 * i));
      out[8 + i] = uint8_t(hi >> (8 * i));
    }
  }

  static Hash128 from_le_bytes(const uint8_t in[16])
  {
    Hash128 r;
    for (int i = 0; i < 8; i++) {
      r.lo |= uint64_t(in[i]) << (8 * i);
      r.hi |= uint64_t(in[8 + i]) << (8 * i);
    }
    return r;
  }

  /** 32 hex digits, hi word first. */
  util::string hex() const
  {
    static const char digits[] = "0123456789abcdef";
    char buf[33];
    for (int i = 0; i < 16; i++) {
      buf[i] = digits[(hi >> (60 - 4 * i)) & 15];
      buf[16 + i] = digits[(lo >> (60 - 4 * i)) & 15];
    }
    buf[32] = 0;
    return util::string(buf);
  }
};

/** Application-defined hash domains. litestl reserves values below 256. */
enum class DomainTag : uint64_t {
  Content = 1,
  Test = 2,
  PoolEntry = 3, /* attr::InternPool entry hashes */
};

/** "litestl.hash128." as two little-endian words. */
constexpr Hash128 kDefaultKey = {0x2e6c74736574696cULL, 0x2e38323168736168ULL};

namespace detail {
constexpr uint64_t rotl(uint64_t x, int b)
{
  return (x << b) | (x >> (64 - b));
}

struct SipState {
  uint64_t v0, v1, v2, v3;

  constexpr void round()
  {
    v0 += v1;
    v1 = rotl(v1, 13);
    v1 ^= v0;
    v0 = rotl(v0, 32);
    v2 += v3;
    v3 = rotl(v3, 16);
    v3 ^= v2;
    v0 += v3;
    v3 = rotl(v3, 21);
    v3 ^= v0;
    v2 += v1;
    v1 = rotl(v1, 17);
    v1 ^= v2;
    v2 = rotl(v2, 32);
  }

  constexpr void compress(uint64_t m)
  {
    v3 ^= m;
    round();
    round();
    v0 ^= m;
  }
};
} // namespace detail

/** Streaming SipHash-2-4-128 over raw bytes (no encoding, no domain). */
class SipHash128 {
public:
  explicit SipHash128(Hash128 key = kDefaultKey)
  {
    s_.v0 = 0x736f6d6570736575ULL ^ key.lo;
    s_.v1 = 0x646f72616e646f6dULL ^ key.hi ^ 0xee;
    s_.v2 = 0x6c7967656e657261ULL ^ key.lo;
    s_.v3 = 0x7465646279746573ULL ^ key.hi;
  }

  void put(const void *data, size_t n)
  {
    const uint8_t *p = static_cast<const uint8_t *>(data);
    total_ += n;
    while (n) {
      if (buffered_ == 0 && n >= 8) {
        uint64_t m;
        memcpy(&m, p, 8); /* little-endian host (static_assert above) */
        s_.compress(m);
        p += 8;
        n -= 8;
        continue;
      }
      buf_ |= uint64_t(*p++) << (8 * buffered_);
      n--;
      if (++buffered_ == 8) {
        s_.compress(buf_);
        buf_ = 0;
        buffered_ = 0;
      }
    }
  }

  Hash128 finish() const
  {
    detail::SipState s = s_;
    const uint64_t b = (uint64_t(total_) << 56) | buf_;
    s.compress(b);
    s.v2 ^= 0xee;
    for (int i = 0; i < 4; i++) {
      s.round();
    }
    Hash128 out;
    out.lo = s.v0 ^ s.v1 ^ s.v2 ^ s.v3;
    s.v1 ^= 0xdd;
    for (int i = 0; i < 4; i++) {
      s.round();
    }
    out.hi = s.v0 ^ s.v1 ^ s.v2 ^ s.v3;
    return out;
  }

private:
  detail::SipState s_;
  uint64_t buf_ = 0;
  int buffered_ = 0;
  uint64_t total_ = 0;
};

/** One-shot raw SipHash-2-4-128. */
inline Hash128 hash128(const void *p, size_t n, Hash128 key = kDefaultKey)
{
  SipHash128 h(key);
  h.put(p, n);
  return h.finish();
}

/* Canonical encoders, usable with any sink exposing put(const void*, size_t).
 * The byte layout is fixed: [type tag][little-endian payload]. */
namespace canonical {
enum Tag : uint8_t {
  TAG_U8 = 1,
  TAG_U32 = 2,
  TAG_I32 = 3,
  TAG_U64 = 4,
  TAG_I64 = 5,
  TAG_BOOL = 6,
  TAG_F32_BITS = 7,
  TAG_F64_BITS = 8,
  TAG_F64_CANONICAL = 9,
  TAG_H128 = 10,
  TAG_STR = 11,
  TAG_DOMAIN = 0xd0,
};

template <typename Sink> void put_le(Sink &s, uint64_t v, int bytes)
{
  uint8_t b[8];
  for (int i = 0; i < bytes; i++) {
    b[i] = uint8_t(v >> (8 * i));
  }
  s.put(b, bytes);
}

template <typename Sink> void tagged(Sink &s, Tag tag, uint64_t v, int bytes)
{
  const uint8_t t = tag;
  s.put(&t, 1);
  put_le(s, v, bytes);
}

constexpr uint64_t f64_canonical_bits(double v)
{
  if (v != v) {
    return 0x7ff8000000000000ULL;
  }
  if (v == 0.0) {
    return 0; /* folds -0.0 into +0.0 */
  }
  return std::bit_cast<uint64_t>(v);
}

template <typename Sink> void u8(Sink &s, uint8_t v) { tagged(s, TAG_U8, v, 1); }
template <typename Sink> void u32(Sink &s, uint32_t v) { tagged(s, TAG_U32, v, 4); }
template <typename Sink> void i32(Sink &s, int32_t v) { tagged(s, TAG_I32, uint32_t(v), 4); }
template <typename Sink> void u64(Sink &s, uint64_t v) { tagged(s, TAG_U64, v, 8); }
template <typename Sink> void i64(Sink &s, int64_t v) { tagged(s, TAG_I64, uint64_t(v), 8); }
template <typename Sink> void boolean(Sink &s, bool v) { tagged(s, TAG_BOOL, v ? 1 : 0, 1); }
template <typename Sink> void f32_bits(Sink &s, float v)
{
  tagged(s, TAG_F32_BITS, std::bit_cast<uint32_t>(v), 4);
}
template <typename Sink> void f64_bits(Sink &s, double v)
{
  tagged(s, TAG_F64_BITS, std::bit_cast<uint64_t>(v), 8);
}
template <typename Sink> void f64_canonical(Sink &s, double v)
{
  tagged(s, TAG_F64_CANONICAL, f64_canonical_bits(v), 8);
}
template <typename Sink> void h128(Sink &s, Hash128 v)
{
  const uint8_t t = TAG_H128;
  s.put(&t, 1);
  put_le(s, v.lo, 8);
  put_le(s, v.hi, 8);
}
template <typename Sink> void str(Sink &s, std::string_view v)
{
  tagged(s, TAG_STR, uint64_t(v.size()), 8);
  s.put(v.data(), v.size());
}
template <typename Sink> void domain(Sink &s, DomainTag d)
{
  tagged(s, TAG_DOMAIN, uint64_t(d), 8);
}
} // namespace canonical

/** Domain-separated, type-tagged tuple hasher. */
class Hasher128 {
public:
  explicit Hasher128(DomainTag d, Hash128 key = kDefaultKey) : sip_(key)
  {
    canonical::domain(sip_, d);
  }

  Hasher128 &u8(uint8_t v) { return canonical::u8(sip_, v), *this; }
  Hasher128 &u32(uint32_t v) { return canonical::u32(sip_, v), *this; }
  Hasher128 &i32(int32_t v) { return canonical::i32(sip_, v), *this; }
  Hasher128 &u64(uint64_t v) { return canonical::u64(sip_, v), *this; }
  Hasher128 &i64(int64_t v) { return canonical::i64(sip_, v), *this; }
  Hasher128 &boolean(bool v) { return canonical::boolean(sip_, v), *this; }
  Hasher128 &f32_bits(float v) { return canonical::f32_bits(sip_, v), *this; }
  Hasher128 &f64_bits(double v) { return canonical::f64_bits(sip_, v), *this; }
  Hasher128 &f64_canonical(double v) { return canonical::f64_canonical(sip_, v), *this; }
  Hasher128 &h128(Hash128 v) { return canonical::h128(sip_, v), *this; }
  Hasher128 &str(std::string_view v) { return canonical::str(sip_, v), *this; }
  /** Untagged bytes: for content hashing of already-canonical buffers. */
  Hasher128 &raw_bytes(const void *p, size_t n) { return sip_.put(p, n), *this; }

  Hash128 finish() const
  {
    return sip_.finish();
  }

private:
  SipHash128 sip_;
};

} // namespace litestl::hash
