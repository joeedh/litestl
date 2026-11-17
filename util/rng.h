#pragma once

/* Counter-based random numbers: Philox4x32 (Salmon et al., "Parallel random
 * numbers: as easy as 1, 2, 3", SC'11; reference: Random123), plus keyed,
 * stateless streams for deterministic procedural generation.
 *
 * Stream layout (pinned; part of the determinism contract):
 *   key     = 64-bit Philox key = stream_key.lo ^ mixHash(stream_key.hi)
 *   counter = {element lo32, element hi32, draw, block}
 *   block 0 is the primary draw; range() rejection consumes lanes of blocks
 *   1, 2, ... of the *same* (element, draw), never draw+1.
 * Results depend only on (stream key, element, draw): call order and thread
 * count are irrelevant. */

#include <array>
#include <cstdint>

#include "math/wideint.h"
#include "util/hash.h"
#include "util/hash128.h"

namespace litestl::rng {

using Counter = std::array<uint32_t, 4>;
using Key = std::array<uint32_t, 2>;

namespace detail {
constexpr void mulhilo32(uint32_t a, uint32_t b, uint32_t &hi, uint32_t &lo)
{
  const uint64_t p = uint64_t(a) * b;
  hi = uint32_t(p >> 32);
  lo = uint32_t(p);
}
} // namespace detail

template <int Rounds = 10> constexpr Counter philox4x32(Counter ctr, Key key)
{
  constexpr uint32_t M0 = 0xD2511F53u, M1 = 0xCD9E8D57u;
  constexpr uint32_t W0 = 0x9E3779B9u, W1 = 0xBB67AE85u;
  for (int r = 0; r < Rounds; r++) {
    if (r) {
      key[0] += W0;
      key[1] += W1;
    }
    uint32_t hi0, lo0, hi1, lo1;
    detail::mulhilo32(M0, ctr[0], hi0, lo0);
    detail::mulhilo32(M1, ctr[2], hi1, lo1);
    ctr = {hi1 ^ ctr[1] ^ key[0], lo1, hi0 ^ ctr[3] ^ key[1], lo0};
  }
  return ctr;
}

class Stream {
public:
  explicit constexpr Stream(hash::Hash128 stream_key)
  {
    const uint64_t k = stream_key.lo ^ hash::mixHash(stream_key.hi);
    key_ = {uint32_t(k), uint32_t(k >> 32)};
  }

  /** Folds a 128-bit persistent ID into a 64-bit element index. Distinct IDs
   * may collide (birthday bound ~2^32 elements per stream); a collision only
   * correlates the two elements' draws. */
  static constexpr uint64_t element_of(hash::Hash128 id)
  {
    return id.lo ^ hash::mixHash(id.hi);
  }

  constexpr Counter block(uint64_t element, uint32_t draw, uint32_t blk) const
  {
    return philox4x32<10>({uint32_t(element), uint32_t(element >> 32), draw, blk}, key_);
  }

  constexpr uint32_t u32(uint64_t element, uint32_t draw = 0) const
  {
    return block(element, draw, 0)[0];
  }

  constexpr uint64_t u64(uint64_t element, uint32_t draw = 0) const
  {
    const Counter c = block(element, draw, 0);
    return uint64_t(c[0]) | (uint64_t(c[1]) << 32);
  }

  /** [0, 1) with 53 random bits. */
  constexpr double uniform(uint64_t element, uint32_t draw = 0) const
  {
    return double(u64(element, draw) >> 11) * 0x1.0p-53;
  }

  /** [0, 1) with 24 random bits. */
  constexpr float uniformf(uint64_t element, uint32_t draw = 0) const
  {
    return float(u32(element, draw) >> 8) * 0x1.0p-24f;
  }

  /** Unbiased integer in [lo, hi_excl) (requires lo < hi_excl). Lemire's
   * multiply-shift with rejection over 64-bit words: block 0 lanes (0,1),
   * (2,3), then block 1, 2, ... */
  constexpr int64_t range(uint64_t element,
                          int64_t lo,
                          int64_t hi_excl,
                          uint32_t draw = 0) const
  {
    const uint64_t span = uint64_t(hi_excl) - uint64_t(lo);
    const uint64_t threshold = (uint64_t(0) - span) % span;
    for (uint32_t blk = 0;; blk++) {
      const Counter c = block(element, draw, blk);
      for (int pair = 0; pair < 2; pair++) {
        const uint64_t x = uint64_t(c[2 * pair]) | (uint64_t(c[2 * pair + 1]) << 32);
        uint64_t hi;
        const uint64_t low = math::detail::umul64(x, span, &hi);
        if (low >= threshold) {
          return int64_t(uint64_t(lo) + hi);
        }
      }
    }
  }

private:
  Key key_{};
};

} // namespace litestl::rng
