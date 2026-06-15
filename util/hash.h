#pragma once

#include "compiler_util.h"
#include "string.h"

#include <bit>
#include <cstdint>
#include <cstring>

namespace litestl::hash {
using HashInt = uint64_t;

template <typename T>
concept HashCls = requires(const T &c) {
  { c.computeHash() } -> std::integral;
};
template <HashCls CLS> inline HashInt hash(const CLS &c)
{
  return c.computeHash();
}

template <typename T> inline HashInt hash(T *ptr)
{
  HashInt h = reinterpret_cast<HashInt>(ptr);

  /* Pointers only use the bottom 48 bits. */
  if constexpr (sizeof(void *) == 8) {
#if 0 // Paranoia check: add final 16 bits to front
    return (h << 16) | (h >> 48);
#else
    return h << 16;
#endif
  } else {
    return h;
  }
}

inline HashInt hash(int i)
{
  return HashInt(i);
}

/* splitmix64 finalizer. Separate from hash() (which must stay stable for
 * existing callers); the open-addressing containers run hash() through this so
 * identity-hashed ints don't cluster under power-of-2 bucket masking. */
inline HashInt mixHash(HashInt h)
{
  h ^= h >> 33;
  h *= 0xff51afd7ed558ccdULL;
  h ^= h >> 33;
  h *= 0xc4ceb9fe1a85ec53ULL;
  h ^= h >> 33;
  return h;
}

/*
 * Swiss-table-style control plane shared by the open-addressing Map and Set.
 *
 * One control byte per slot: kEmpty (0x80) or kDeleted (0xFE, a tombstone) —
 * both with the high bit set — or FULL, the high bit clear plus a 7-bit hash
 * fragment (h2). A group of kGroupWidth (8) control bytes is matched at once via
 * portable uint64 SWAR (no SSE; works on native + WASM). match() returns a
 * bitmask with the high bit set in each matching lane (in memory byte order).
 */
namespace swiss {
inline constexpr uint8_t kEmpty = 0x80;
inline constexpr uint8_t kDeleted = 0xFE;
inline constexpr int kGroupWidth = 8;

inline bool ctrlIsFull(uint8_t c)
{
  return (c & 0x80) == 0;
}

/* 7-bit fragment from the high bits of the mixed hash, decorrelated from the low
 * bits that pick the bucket. */
inline uint8_t h2(HashInt mixed)
{
  return uint8_t((mixed >> 57) & 0x7f);
}

struct Group {
  static constexpr uint64_t kLSBs = 0x0101010101010101ULL;
  static constexpr uint64_t kMSBs = 0x8080808080808080ULL;
  uint64_t ctrl;

  explicit Group(const uint8_t *p)
  {
    std::memcpy(&ctrl, p, kGroupWidth);
  }

  /* High bit set per lane whose byte == c (Mycroft zero-byte test). Works for
   * any byte value, including kEmpty/kDeleted. */
  uint64_t match(uint8_t c) const
  {
    uint64_t x = ctrl ^ (kLSBs * c);
    return (x - kLSBs) & ~x & kMSBs;
  }
  uint64_t matchEmpty() const
  {
    return match(kEmpty);
  }
  uint64_t matchDeleted() const
  {
    return match(kDeleted);
  }
  /* Any non-FULL lane (empty or deleted): the high bit of each is already set. */
  uint64_t matchFree() const
  {
    return ctrl & kMSBs;
  }
};

/* Index (0..kGroupWidth-1) of the lowest matching lane in a match mask. All
 * supported targets are little-endian. */
inline int lowestLane(uint64_t mask)
{
  if constexpr (std::endian::native == std::endian::little) {
    return int(std::countr_zero(mask) >> 3);
  } else {
    return int(std::countl_zero(mask) >> 3);
  }
}
} // namespace swiss

/** NOT cryptographically secure! */
inline HashInt hash(const char *str)
{
  const char *c = str;
  HashInt h = 0;

  /* TODO: check this. */
  for (; *c; c++) {
    h = ((h + *c) * (*c) + 23423432) & ((1 << 19) - 1);
  }

  return h;
}
inline HashInt hash(const util::string &str)
{
  return hash(str.c_str());
}
inline HashInt hash(const util::stringref &str)
{
  return hash(str.c_str());
}
} // namespace litestl::hash
