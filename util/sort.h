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
 *    fixed by this implementation (input order), on every platform. */

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
    const size_t hi = std::min(n, lo + kRun);
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
      const size_t mid = std::min(n, lo + width);
      const size_t hi = std::min(n, lo + 2 * width);
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

} // namespace litestl::util
