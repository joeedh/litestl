#pragma once

/* PagedStore<T>: paged, copy-on-write storage for one attribute column.
 *
 * Pages are Absent (all elements equal the store default), Constant (one
 * stored value) or Dense (an RcBuffer shared between snapshots). The page
 * size is a per-store runtime constant (2^page_shift elements). The last page
 * has partial capacity (power of two >= its live count, min 16, max page),
 * so small stores stay small.
 *
 * Copy-on-write: any write to a page first makes it a unique Dense buffer
 * (copying a shared one), including the default reset of newly exposed tail
 * elements on grow. share() is O(pages).
 *
 * Reference rules: a T& from write() or a span from page_write() is
 * invalidated by any later call on the store, including share(). Concurrent
 * writes are allowed only through spans obtained serially (page_write) or
 * handed out by Column::parallel_write; never call write()/set()/structural
 * methods concurrently.
 *
 * Equality is bitwise everywhere (value types must be padding-free), so
 * -0.0 != +0.0 and identical NaN payloads compare equal.
 *
 * With LITESTL_ATTR_CHECKS (default in builds without NDEBUG), share()
 * records a checksum of every Dense page that becomes shared and verifies it
 * when the buffer is later copied on write or released while shared: a
 * mismatch means something wrote through a stale reference into a shared
 * page. */

#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>

#include "util/rc_buffer.h"
#include "util/span.h"
#include "util/vector.h"

#ifndef LITESTL_ATTR_CHECKS
#ifdef NDEBUG
#define LITESTL_ATTR_CHECKS 0
#else
#define LITESTL_ATTR_CHECKS 1
#endif
#endif

namespace litestl::attr {

constexpr int kDefaultPageShift = 12;
constexpr uint32_t kTailMinCapacity = 16;

enum class PageState : uint8_t { Absent = 0, Constant = 1, Dense = 2 };

/* ------------------------------------------------------------------------
 * Value requirements
 * ---------------------------------------------------------------------- */

/** Customisation point: specialise to true for padding-free user types whose
 * object representation is canonical up to bitwise equality. */
template <typename T> struct is_padding_free_custom : std::false_type {};

template <typename T> constexpr bool is_padding_free_v();

namespace detail {
template <typename T>
concept HasVecShape = requires {
  typename T::value_type;
  { T::size } -> std::convertible_to<int>;
};

template <typename T> struct ArrayShape : std::false_type {};
template <typename E, size_t N> struct ArrayShape<std::array<E, N>> : std::true_type {
  using elem = E;
  static constexpr size_t n = N;
};
} // namespace detail

template <typename T> constexpr bool is_padding_free_v()
{
  if constexpr (is_padding_free_custom<T>::value || std::has_unique_object_representations_v<T> ||
                std::is_floating_point_v<T>)
  {
    return true;
  }
  else if constexpr (detail::ArrayShape<T>::value) {
    using E = typename detail::ArrayShape<T>::elem;
    return is_padding_free_v<E>() && sizeof(T) == detail::ArrayShape<T>::n * sizeof(E);
  }
  else if constexpr (detail::HasVecShape<T>) {
    using E = typename T::value_type;
    return is_padding_free_v<E>() && sizeof(T) == size_t(T::size) * sizeof(E);
  }
  else {
    return false;
  }
}

template <typename T>
concept ColumnValue = std::is_trivially_copyable_v<T> && std::is_default_constructible_v<T> &&
                      (is_padding_free_v<T>());

template <ColumnValue T> inline bool bit_equal(const T &a, const T &b)
{
  return memcmp(static_cast<const void *>(&a), static_cast<const void *>(&b), sizeof(T)) == 0;
}

/** Process-wide version stamps: every mutation of any store ends up with a
 * never-before-seen value, so stamps from diverged copies can't collide. */
inline uint64_t next_version()
{
  static std::atomic<uint64_t> counter{0};
  return counter.fetch_add(1, std::memory_order_relaxed) + 1;
}

/* ------------------------------------------------------------------------
 * PagedStore
 * ---------------------------------------------------------------------- */

template <ColumnValue T> class PagedStore {
  struct Page {
    PageState state = PageState::Absent;
    T value{};                      /* Constant pages */
    util::RcBuffer<T> buf;          /* Dense pages */
#if LITESTL_ATTR_CHECKS
    mutable uint64_t shared_sum = 0; /* checksum recorded by share() */
    mutable bool sum_valid = false;
#endif
  };

  util::Vector<Page, 1> pages_;
  uint32_t size_ = 0;
  uint32_t shift_;
  uint32_t mask_;
  T default_{};
  mutable uint64_t version_ = 0;
  mutable bool dirty_ = true;

#if LITESTL_ATTR_CHECKS
  static uint64_t checksum(const T *p, uint32_t n)
  {
    /* FNV-1a over bytes: cheap, debug only */
    const uint8_t *b = reinterpret_cast<const uint8_t *>(p);
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < size_t(n) * sizeof(T); i++) {
      h = (h ^ b[i]) * 1099511628211ull;
    }
    return h;
  }

  void verify_shared(const Page &pg) const
  {
    if (pg.sum_valid && pg.buf && checksum(pg.buf.data(), pg.buf.capacity()) != pg.shared_sum) {
      fprintf(stderr,
              "litestl::attr: a shared page was modified (write through a reference held "
              "across share()?)\n");
      abort();
    }
  }
#endif

  void touch()
  {
    dirty_ = true;
  }

  uint32_t live_count(uint32_t p) const
  {
    const uint64_t start = uint64_t(p) << shift_;
    return start >= size_ ? 0 : uint32_t(std::min<uint64_t>(page_size(), size_ - start));
  }

  uint32_t capacity_for(uint32_t p, uint32_t needed) const
  {
    if (p + 1 < pages_.size()) {
      return page_size();
    }
    uint32_t c = std::min(kTailMinCapacity, page_size());
    while (c < needed) {
      c <<= 1;
    }
    return std::min(c, page_size());
  }

  const T &uniform_value(const Page &pg) const
  {
    return pg.state == PageState::Constant ? pg.value : default_;
  }

  /* Make page p a unique Dense buffer with capacity >= needed. Existing live
   * contents are preserved; elements beyond the old buffer are filled with
   * the page's previous uniform value (Absent/Constant) or left as-is. */
  T *make_unique_dense(uint32_t p, uint32_t needed)
  {
    Page &pg = pages_[p];
    if (pg.state == PageState::Dense) {
      const bool unique = pg.buf.unique();
      if (unique && pg.buf.capacity() >= needed) {
#if LITESTL_ATTR_CHECKS
        pg.sum_valid = false;
#endif
        return pg.buf.data();
      }
#if LITESTL_ATTR_CHECKS
      if (!unique) {
        verify_shared(pg);
      }
      pg.sum_valid = false;
#endif
      const uint32_t cap = std::max(capacity_for(p, needed), pg.buf.capacity());
      util::RcBuffer<T> nb = util::RcBuffer<T>::copy_of(pg.buf.data(), pg.buf.capacity(), cap);
      for (uint32_t i = pg.buf.capacity(); i < cap; i++) {
        nb.data()[i] = default_;
      }
      pg.buf = std::move(nb);
      return pg.buf.data();
    }
    const T v = uniform_value(pg);
    const uint32_t cap = capacity_for(p, std::max(needed, live_count(p)));
    pg.buf = util::RcBuffer<T>::make(cap);
    for (uint32_t i = 0; i < cap; i++) {
      pg.buf.data()[i] = v;
    }
    pg.state = PageState::Dense;
#if LITESTL_ATTR_CHECKS
    pg.sum_valid = false;
#endif
    return pg.buf.data();
  }

  void set_uniform(uint32_t p, const T &v)
  {
    Page &pg = pages_[p];
#if LITESTL_ATTR_CHECKS
    if (pg.state == PageState::Dense && !pg.buf.unique()) {
      verify_shared(pg);
    }
    pg.sum_valid = false;
#endif
    pg.buf.reset();
    if (bit_equal(v, default_)) {
      pg.state = PageState::Absent;
      pg.value = T{};
    }
    else {
      pg.state = PageState::Constant;
      pg.value = v;
    }
  }

public:
  explicit PagedStore(const T &default_value = T{}, int page_shift = kDefaultPageShift)
      : shift_(uint32_t(page_shift)), mask_((1u << page_shift) - 1), default_(default_value)
  {
    if (page_shift < 1 || page_shift > 20) {
      fprintf(stderr, "litestl::attr: page_shift %d out of range [1, 20]\n", page_shift);
      abort();
    }
  }

  PagedStore(const PagedStore &) = delete; /* use share() */
  PagedStore &operator=(const PagedStore &) = delete;
  PagedStore(PagedStore &&) noexcept = default;
  PagedStore &operator=(PagedStore &&) noexcept = default;

  ~PagedStore()
  {
#if LITESTL_ATTR_CHECKS
    for (int p = 0; p < pages_.size(); p++) {
      if (pages_[p].state == PageState::Dense && !pages_[p].buf.unique()) {
        verify_shared(pages_[p]);
      }
    }
#endif
  }

  uint32_t size() const
  {
    return size_;
  }
  uint32_t page_shift() const
  {
    return shift_;
  }
  uint32_t page_size() const
  {
    return 1u << shift_;
  }
  uint32_t page_count() const
  {
    return uint32_t(pages_.size());
  }
  const T &default_value() const
  {
    return default_;
  }

  /** Version stamp; changes (to a never-before-seen value) after any mutation. */
  uint64_t version() const
  {
    if (dirty_) {
      version_ = next_version();
      dirty_ = false;
    }
    return version_;
  }

  /** Marks the store mutated (used after concurrent span writes). */
  void mark_modified()
  {
    touch();
  }

  const T &get(uint32_t i) const
  {
    const Page &pg = pages_[i >> shift_];
    switch (pg.state) {
      case PageState::Dense:
        return pg.buf.data()[i & mask_];
      case PageState::Constant:
        return pg.value;
      default:
        return default_;
    }
  }

  T &write(uint32_t i)
  {
    touch();
    return make_unique_dense(i >> shift_, (i & mask_) + 1)[i & mask_];
  }

  void set(uint32_t i, const T &v)
  {
    const Page &pg = pages_[i >> shift_];
    if (pg.state != PageState::Dense && bit_equal(uniform_value(pg), v)) {
      return;
    }
    write(i) = v;
  }

  void resize(uint32_t n)
  {
    touch();
    const uint32_t old = size_;
    const uint32_t npages = n ? ((n - 1) >> shift_) + 1 : 0;
    if (n < old) {
      pages_.resize(int(npages));
      size_ = n;
      return;
    }
    if (n == old) {
      return;
    }
    /* reset the newly exposed part of the old last page (A3: copy first) */
    if (old > 0 && (old & mask_) != 0) {
      const uint32_t p = (old - 1) >> shift_;
      Page &pg = pages_[p];
      const uint32_t first = old & mask_;
      const uint32_t last = std::min(page_size(), n - (p << shift_)); /* exclusive */
      if (pg.state == PageState::Dense) {
        T *d = make_unique_dense(p, last);
        for (uint32_t i = first; i < last; i++) {
          d[i] = default_;
        }
      }
      else if (pg.state == PageState::Constant) {
        /* mixed: old live part = constant, new part = default */
        size_ = old;
        T *d = make_unique_dense(p, last);
        for (uint32_t i = first; i < last; i++) {
          d[i] = default_;
        }
      }
    }
    pages_.resize(int(npages));
    size_ = n;
  }

  void fill(uint32_t first, uint32_t n, const T &v)
  {
    if (n == 0) {
      return;
    }
    touch();
    const uint32_t end = first + n;
    uint32_t i = first;
    while (i < end) {
      const uint32_t p = i >> shift_;
      const uint32_t page_start = p << shift_;
      const uint32_t live = live_count(p);
      const uint32_t stop = std::min(end, page_start + live);
      if ((i == page_start) && (stop == page_start + live)) {
        set_uniform(p, v);
      }
      else {
        T *d = make_unique_dense(p, stop - page_start);
        for (uint32_t k = i - page_start; k < stop - page_start; k++) {
          d[k] = v;
        }
      }
      i = stop;
    }
  }

  void set_default_range(uint32_t first, uint32_t n)
  {
    fill(first, n, default_);
  }

  PageState page_state(uint32_t p) const
  {
    return pages_[p].state;
  }

  uint32_t page_live_count(uint32_t p) const
  {
    return live_count(p);
  }

  /** Live elements of page p. Absent/Constant pages are expanded into scratch. */
  util::span<const T> page_span(uint32_t p, util::Vector<T> &scratch) const
  {
    const Page &pg = pages_[p];
    const uint32_t live = live_count(p);
    if (pg.state == PageState::Dense) {
      return util::span<const T>(pg.buf.data(), live);
    }
    scratch.resize(int(live));
    const T &v = uniform_value(pg);
    for (uint32_t i = 0; i < live; i++) {
      scratch[i] = v;
    }
    return util::span<const T>(scratch.data(), live);
  }

  /** Uniform value of an Absent/Constant page (nullptr for Dense). */
  const T *page_uniform(uint32_t p) const
  {
    const Page &pg = pages_[p];
    return pg.state == PageState::Dense ? nullptr : &uniform_value(pg);
  }

  /** Unique, writable live elements of page p (marks the store modified). */
  util::span<T> page_write(uint32_t p)
  {
    touch();
    const uint32_t live = live_count(p);
    return util::span<T>(make_unique_dense(p, live), live);
  }

  /** Makes every page overlapping [first, first+n) unique and Dense, so spans
   * of them can be written from several threads. */
  void prepare_write(uint32_t first, uint32_t n)
  {
    if (n == 0) {
      return;
    }
    touch();
    for (uint32_t p = first >> shift_; p <= (first + n - 1) >> shift_; p++) {
      make_unique_dense(p, live_count(p));
    }
  }

  /** Live bytes of a Dense page (empty for Absent/Constant). */
  util::span<const uint8_t> page_bytes(uint32_t p) const
  {
    const Page &pg = pages_[p];
    if (pg.state != PageState::Dense) {
      return {};
    }
    return util::span<const uint8_t>(reinterpret_cast<const uint8_t *>(pg.buf.data()),
                                     size_t(live_count(p)) * sizeof(T));
  }

  /** True when page p is known identical in both stores: same live length and
   * either the same buffer or equal uniform values (Absent == Constant(default)).
   * May return false for equal contents held in distinct buffers. */
  bool same_page_as(const PagedStore &o, uint32_t p) const
  {
    if (p >= page_count() || p >= o.page_count() || live_count(p) != o.live_count(p)) {
      return false;
    }
    const Page &a = pages_[p];
    const Page &b = o.pages_[p];
    if (a.state == PageState::Dense || b.state == PageState::Dense) {
      return a.state == b.state && a.buf.id() == b.buf.id();
    }
    return bit_equal(uniform_value(a), o.uniform_value(b));
  }

  bool shares_buffer_with(const PagedStore &o, uint32_t p) const
  {
    return p < page_count() && p < o.page_count() && pages_[p].state == PageState::Dense &&
           pages_[p].buf.id() == o.pages_[p].buf.id();
  }

  /** O(pages) copy-on-write snapshot. */
  PagedStore share() const
  {
    PagedStore r(default_, int(shift_));
    r.pages_.resize(pages_.size());
    for (int p = 0; p < pages_.size(); p++) {
      const Page &src = pages_[p];
      Page &dst = r.pages_[p];
      dst.state = src.state;
      dst.value = src.value;
      dst.buf = src.buf;
#if LITESTL_ATTR_CHECKS
      if (src.state == PageState::Dense) {
        if (!src.sum_valid) {
          src.shared_sum = checksum(src.buf.data(), src.buf.capacity());
          src.sum_valid = true;
        }
        dst.shared_sum = src.shared_sum;
        dst.sum_valid = true;
      }
#endif
    }
    r.size_ = size_;
    r.version_ = version();
    r.dirty_ = false;
    return r;
  }
};

} // namespace litestl::attr
