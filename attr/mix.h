#pragma once

/* Mix batches: one entry per target element,
 *   target[k] <- mix(sources[offsets[k] .. offsets[k+1]), weights[...])
 * with simultaneous semantics: every source value is read from the pre-call
 * state before any target is written, so targets may alias sources (e.g.
 * swapping the two sides of a face). An empty source list sets the default.
 *
 * A batch is validated once (AttrGroup::mix, or validate() directly) and the
 * resulting ValidatedMixBatch is what column virtuals accept. */

#include <algorithm>
#include <cstdint>

#include "../util/span.h"
#include "../util/vector.h"

namespace litestl::attr {

class AttrGroup;

enum class MixPolicy : uint8_t { Skip, SetDefault, CopyFirst, Interpolate, WeightedMean, Sum, Custom };

struct MixBatch {
  util::span<const uint32_t> targets;
  util::span<const uint32_t> offsets; /* targets.size() + 1 */
  util::span<const uint32_t> sources;
  util::span<const double> weights; /* empty, or sources.size() */
};

class ValidatedMixBatch {
public:
  const MixBatch &batch() const
  {
    return b_;
  }
  uint32_t target_size() const
  {
    return target_size_;
  }
  uint32_t source_size() const
  {
    return source_size_;
  }

  /** Checks structure and ranges; targets must be unique (sorted-copy check,
   * O(T log T)). On failure returns false and sets *err. */
  static bool validate(const MixBatch &b,
                       uint32_t target_size,
                       uint32_t source_size,
                       ValidatedMixBatch &out,
                       const char **err)
  {
    auto fail = [&](const char *msg) {
      if (err) {
        *err = msg;
      }
      return false;
    };
    if (b.offsets.size() != b.targets.size() + 1) {
      return fail("mix: offsets.size() must be targets.size() + 1");
    }
    if (b.offsets[0] != 0) {
      return fail("mix: offsets[0] must be 0");
    }
    for (size_t k = 0; k < b.targets.size(); k++) {
      if (b.offsets[k + 1] < b.offsets[k]) {
        return fail("mix: offsets must be non-decreasing");
      }
    }
    if (b.offsets.back() != b.sources.size()) {
      return fail("mix: offsets.back() must equal sources.size()");
    }
    if (!b.weights.empty() && b.weights.size() != b.sources.size()) {
      return fail("mix: weights must be empty or match sources");
    }
    for (uint32_t t : b.targets) {
      if (t >= target_size) {
        return fail("mix: target index out of range");
      }
    }
    for (uint32_t s : b.sources) {
      if (s >= source_size) {
        return fail("mix: source index out of range");
      }
    }
    util::Vector<uint32_t, 16> sorted;
    sorted.resize(int(b.targets.size()));
    for (size_t k = 0; k < b.targets.size(); k++) {
      sorted[int(k)] = b.targets[k];
    }
    std::sort(sorted.begin(), sorted.end());
    for (int k = 1; k < sorted.size(); k++) {
      if (sorted[k] == sorted[k - 1]) {
        return fail("mix: duplicate target");
      }
    }
    out.b_ = b;
    out.target_size_ = target_size;
    out.source_size_ = source_size;
    return true;
  }

private:
  MixBatch b_;
  uint32_t target_size_ = 0;
  uint32_t source_size_ = 0;
};

/** Context for Custom mixes: `pre` is a snapshot of the target group taken
 * before the call (read siblings from it, never from the live group); `src`
 * is the source group for mix_from (null otherwise); `op` is operator data. */
struct MixContext {
  const AttrGroup *pre = nullptr;
  const AttrGroup *src = nullptr;
  const void *op = nullptr;
  uint32_t slot = 0;
};

} // namespace litestl::attr
