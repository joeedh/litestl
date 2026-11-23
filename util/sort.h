#pragma once

/* Deterministic sorting.
 *
 * std::sort and std::stable_sort are not specified to produce the same
 * permutation across standard libraries when keys tie (and std::sort not even
 * across versions of one library). Two ways to stay reproducible:
 *
 *  - sort_unique_keys: the comparator is a strict total order with no ties
 *    among the inputs. Every correct sort then yields the same permutation, so
 *    std::sort is fine. Ties are *checked* (in every build type) and reported,
 *    because a tie is a caller bug (e.g. a duplicate persistent ID).
 *  - stable_sort: a self-contained merge sort whose output for tied keys is
 *    fixed by this implementation (input order), on every platform.
 *
 * group_by_key is a stable counting sort of indices by a small integer key,
 * into compressed (CSR) form: one flat array plus per-key offsets, in place
 * of a vector of vectors. */

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "util/span.h"
#include "util/vector.h"

namespace litestl::util {

/** Sorts @p s by @p less. Returns false if two adjacent sorted elements are
 * equivalent (neither less than the other); @p tie_index then receives the
 * index i (in sorted order) with s[i] equivalent to s[i + 1]. The span is
 * sorted either way. */
template <typename T, typename Less>
bool sort_unique_keys(span<T> s, Less less, size_t *tie_index = nullptr)
{
  std::sort(s.begin(), s.end(), less);
  for (size_t i = 1; i < s.size(); i++) {
    if (!less(s[i - 1], s[i])) {
      if (tie_index) {
        *tie_index = i - 1;
      }
      return false;
    }
  }
  return true;
}

/** Stable merge sort (bottom-up, ping-pong buffer). T must be copyable. */
template <typename T, typename Less> void stable_sort(span<T> s, Less less)
{
  const size_t n = s.size();
  if (n < 2) {
    return;
  }
  /* Insertion-sort runs of 16 first: stable, and cheap on short runs. */
  constexpr size_t kRun = 16;
  for (size_t lo = 0; lo < n; lo += kRun) {
    const size_t hi = (std::min)(n, lo + kRun);
    for (size_t i = lo + 1; i < hi; i++) {
      T v = s[i];
      size_t j = i;
      while (j > lo && less(v, s[j - 1])) {
        s[j] = s[j - 1];
        j--;
      }
      s[j] = v;
    }
  }
  if (n <= kRun) {
    return;
  }
  Vector<T> buf;
  buf.resize(n);
  T *src = s.data();
  T *dst = buf.data();
  for (size_t width = kRun; width < n; width *= 2) {
    for (size_t lo = 0; lo < n; lo += 2 * width) {
      const size_t mid = (std::min)(n, lo + width);
      const size_t hi = (std::min)(n, lo + 2 * width);
      size_t a = lo, b = mid, o = lo;
      while (a < mid && b < hi) {
        /* take from the right run only if strictly less: keeps stability */
        if (less(src[b], src[a])) {
          dst[o++] = src[b++];
        }
        else {
          dst[o++] = src[a++];
        }
      }
      while (a < mid) {
        dst[o++] = src[a++];
      }
      while (b < hi) {
        dst[o++] = src[b++];
      }
    }
    std::swap(src, dst);
  }
  if (src != s.data()) {
    std::copy(src, src + n, s.data());
  }
}

/** Indices grouped by key: group k is items[offsets[k] .. offsets[k + 1]). */
struct KeyGroups {
  Vector<uint32_t> offsets; /* group count + 1 entries */
  Vector<uint32_t> items;

  uint32_t size() const
  {
    return offsets.size() ? uint32_t(offsets.size() - 1) : 0u;
  }
  span<const uint32_t> operator[](uint32_t k) const
  {
    return span<const uint32_t>(items.data() + offsets[k], offsets[k + 1] - offsets[k]);
  }
};

/** Key that leaves an index out of every group. */
inline constexpr uint32_t kNoGroup = 0xffffffffu;

/** Groups the indices 0..n-1 by key(i) < nkeys (or kNoGroup), in O(n + nkeys).
 * Stable: each group lists its indices in ascending order. key is called
 * twice per index and must return the same value both times. */
template <typename KeyFn> void group_by_key(uint32_t n, uint32_t nkeys, KeyFn &&key, KeyGroups &out)
{
  out.offsets.resize(size_t(nkeys) + 1);
  for (uint32_t k = 0; k <= nkeys; k++) {
    out.offsets[k] = 0;
  }
  for (uint32_t i = 0; i < n; i++) {
    const uint32_t k = key(i);
    if (k != kNoGroup) {
      out.offsets[k + 1]++;
    }
  }
  for (uint32_t k = 0; k < nkeys; k++) {
    out.offsets[k + 1] += out.offsets[k];
  }
  out.items.resize(out.offsets[nkeys]);
  /* fill from the back of each group, scanning backwards: stays stable and
   * needs no cursor array */
  for (uint32_t i = n; i-- > 0;) {
    const uint32_t k = key(i);
    if (k != kNoGroup) {
      out.items[--out.offsets[k + 1]] = i;
    }
  }
  /* offsets[k + 1] now holds group k's start: shift down one place */
  for (uint32_t k = 0; k < nkeys; k++) {
    out.offsets[k] = out.offsets[k + 1];
  }
  out.offsets[nkeys] = uint32_t(out.items.size());
}

} // namespace litestl::util
