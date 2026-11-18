#pragma once

/* ValueOps<T>: per-value-type mixing behaviour, selected by concepts (no
 * central switch over types). Families:
 *   float-like  (float/double scalars, Vec<F,N>, std::array<F,N>)
 *     Interpolate = sum(w*s); WeightedMean = sum(w*s)/sum(w) (CopyFirst when
 *     sum(w) == 0); Sum = sum(s). Accumulated in double, left to right in
 *     source order, converted to T once. NaN results are canonicalized
 *     (0x7fc00000 / 0x7ff8000000000000): arithmetic NaN payloads are not
 *     deterministic. CopyFirst preserves payloads. Default policy: Interpolate.
 *   integral    (integer scalars/vectors/arrays)
 *     Sum accumulates in the column's own width with defined wraparound.
 *     Interpolate/WeightedMean compute in double, round half away from zero
 *     and saturate to the type's range (NaN -> 0). Precision is limited to
 *     2^53 for 64-bit types. Default policy: CopyFirst.
 *   other       (IDs, hashes, references, enums, user structs)
 *     CopyFirst / SetDefault / Skip / Custom only. Default policy: CopyFirst.
 *
 * Mix kernels must not be FP-contracted: the attr target sets PUBLIC
 * -ffp-contract=off, and kernels open a block-scoped clang pragma. A custom
 * ValueOps specialisation must follow the same rules to stay deterministic.
 *
 * Canonical encoding is the raw little-endian object representation, which
 * is canonical because ColumnValue types are padding-free (paged_store.h). */

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

#include "attr/mix.h"
#include "attr/paged_store.h"

#if defined(__FAST_MATH__)
#error "litestl::attr must not be compiled with -ffast-math (deterministic mixing)"
#endif

namespace litestl::attr {

namespace detail {
/* Component view of scalars, Vec<E,N> and std::array<E,N>. */
template <typename T> struct Components {
  using elem = T;
  static constexpr int n = 1;
};
template <typename T>
  requires HasVecShape<T>
struct Components<T> {
  using elem = typename T::value_type;
  static constexpr int n = T::size;
};
template <typename E, size_t N> struct Components<std::array<E, N>> {
  using elem = E;
  static constexpr int n = int(N);
};

template <typename T> using elem_t = typename Components<T>::elem;

template <typename T> inline void load(const T &v, elem_t<T> *out)
{
  memcpy(static_cast<void *>(out), static_cast<const void *>(&v), sizeof(T));
}
template <typename T> inline T store(const elem_t<T> *in)
{
  T v;
  memcpy(static_cast<void *>(&v), static_cast<const void *>(in), sizeof(T));
  return v;
}

/* IEEE leaves the payload of a NaN produced by arithmetic unspecified, and
 * compilers may legally swap commutative operands, so arithmetic NaN results
 * are canonicalized to the default quiet NaN for determinism. */
template <typename F> inline F canonical_nan_if(F v)
{
  if (v == v) {
    return v;
  }
  if constexpr (sizeof(F) == 4) {
    return std::bit_cast<F>(uint32_t(0x7fc00000u));
  }
  else {
    return std::bit_cast<F>(uint64_t(0x7ff8000000000000ull));
  }
}

template <typename I> inline I saturate_round(double x)
{
  if (x != x) {
    return I(0);
  }
  const double r = std::round(x); /* half away from zero */
  if (r <= double(std::numeric_limits<I>::lowest())) {
    return std::numeric_limits<I>::lowest();
  }
  if (r >= double(std::numeric_limits<I>::max())) {
    return std::numeric_limits<I>::max();
  }
  return I(r);
}
} // namespace detail

template <typename T>
concept FloatLike = std::is_floating_point_v<detail::elem_t<T>>;
template <typename T>
concept IntegralLike = std::is_integral_v<detail::elem_t<T>> && !std::is_same_v<detail::elem_t<T>, bool>;

template <typename T> struct ValueOps {
  static constexpr MixPolicy default_policy = MixPolicy::CopyFirst;
  static constexpr bool supports(MixPolicy p)
  {
    return p == MixPolicy::Skip || p == MixPolicy::SetDefault || p == MixPolicy::CopyFirst ||
           p == MixPolicy::Custom;
  }
  /* only called for policies with supports(p) && p in {Interpolate, WeightedMean, Sum} */
  static T combine(MixPolicy, const T *src, const double *, int)
  {
    return src[0];
  }
};

template <typename T>
  requires FloatLike<T> || IntegralLike<T>
struct ValueOps<T> {
  using E = detail::elem_t<T>;
  static constexpr int N = detail::Components<T>::n;
  static constexpr MixPolicy default_policy =
      FloatLike<T> ? MixPolicy::Interpolate : MixPolicy::CopyFirst;

  static constexpr bool supports(MixPolicy)
  {
    return true;
  }

  /* n >= 1; weights may be null (treated as all 1). */
  static T combine(MixPolicy p, const T *src, const double *w, int n)
  {
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
    E out[N];
    if (p == MixPolicy::Sum) {
      for (int c = 0; c < N; c++) {
        if constexpr (FloatLike<T>) {
          double acc = 0.0;
          for (int i = 0; i < n; i++) {
            E comps[N];
            detail::load(src[i], comps);
            acc += double(comps[c]);
          }
          out[c] = detail::canonical_nan_if(E(acc));
        }
        else {
          using U = std::make_unsigned_t<E>;
          U acc = 0;
          for (int i = 0; i < n; i++) {
            E comps[N];
            detail::load(src[i], comps);
            acc = U(acc + U(comps[c]));
          }
          out[c] = E(acc);
        }
      }
      return detail::store<T>(out);
    }
    /* Interpolate / WeightedMean */
    double wsum = 0.0;
    for (int i = 0; i < n; i++) {
      wsum += w ? w[i] : 1.0;
    }
    if (p == MixPolicy::WeightedMean && wsum == 0.0) {
      return src[0];
    }
    for (int c = 0; c < N; c++) {
      double acc = 0.0;
      for (int i = 0; i < n; i++) {
        E comps[N];
        detail::load(src[i], comps);
        const double wi = w ? w[i] : 1.0;
        const double term = wi * double(comps[c]);
        acc += term;
      }
      if (p == MixPolicy::WeightedMean) {
        acc = acc / wsum;
      }
      if constexpr (FloatLike<T>) {
        out[c] = detail::canonical_nan_if(E(acc));
      }
      else {
        out[c] = detail::saturate_round<E>(acc);
      }
    }
    return detail::store<T>(out);
  }
};

} // namespace litestl::attr
