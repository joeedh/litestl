#pragma once

/* RcBuffer<T>: an intrusive, atomically refcounted, alloc-tracked array of
 * trivially copyable T, for copy-on-write storage shared between snapshots
 * that may be read on different threads.
 *
 * Thread contract: distinct handles to one buffer may be copied and destroyed
 * concurrently. Only the thread that owns a handle may test unique() and then
 * write through it (the usual COW argument: a unique handle has no other
 * owner to race with). Writing through a non-unique handle is a bug.
 *
 * data() is aligned to max(alignof(T), 16), independent of the allocator's
 * own (platform-dependent) alignment. */

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

#include "alloc.h"

namespace litestl::util {

template <typename T> class RcBuffer {
  static_assert(std::is_trivially_copyable_v<T>, "RcBuffer holds trivially copyable values");

  struct Header {
    std::atomic<uint32_t> refs;
    uint32_t capacity;
    void *block; /* pointer returned by alloc::alloc, for release */
  };

  static constexpr size_t kAlign = alignof(T) > 16 ? alignof(T) : 16;
  static constexpr size_t kHeaderSize = (sizeof(Header) + kAlign - 1) / kAlign * kAlign;

  Header *h_ = nullptr;

  explicit RcBuffer(Header *h) : h_(h) {}

  T *elems() const
  {
    return reinterpret_cast<T *>(reinterpret_cast<char *>(h_) + kHeaderSize);
  }

  void release()
  {
    if (h_ && h_->refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      void *block = h_->block;
      h_->~Header();
      alloc::release(block);
    }
    h_ = nullptr;
  }

public:
  RcBuffer() = default;

  /** New buffer with uninitialised elements. capacity 0 yields an empty handle. */
  static RcBuffer make(uint32_t capacity)
  {
    if (capacity == 0) {
      return RcBuffer();
    }
    const size_t bytes = kHeaderSize + size_t(capacity) * sizeof(T) + kAlign;
    void *block = alloc::alloc("RcBuffer", bytes);
    const uintptr_t base = (reinterpret_cast<uintptr_t>(block) + kAlign - 1) & ~uintptr_t(kAlign - 1);
    Header *h = new (reinterpret_cast<void *>(base)) Header{{1}, capacity, block};
    return RcBuffer(h);
  }

  /** New buffer of @p capacity holding a copy of src[0..n) (n <= capacity);
   * elements [n, capacity) are uninitialised. */
  static RcBuffer copy_of(const T *src, uint32_t n, uint32_t capacity)
  {
    RcBuffer r = make(capacity);
    if (n) {
      memcpy(static_cast<void *>(r.elems()), static_cast<const void *>(src), size_t(n) * sizeof(T));
    }
    return r;
  }

  RcBuffer(const RcBuffer &b) : h_(b.h_)
  {
    if (h_) {
      h_->refs.fetch_add(1, std::memory_order_relaxed);
    }
  }

  RcBuffer(RcBuffer &&b) noexcept : h_(b.h_)
  {
    b.h_ = nullptr;
  }

  RcBuffer &operator=(const RcBuffer &b)
  {
    if (b.h_ != h_) {
      RcBuffer tmp(b);
      std::swap(h_, tmp.h_);
    }
    return *this;
  }

  RcBuffer &operator=(RcBuffer &&b) noexcept
  {
    if (this != &b) {
      release();
      h_ = b.h_;
      b.h_ = nullptr;
    }
    return *this;
  }

  ~RcBuffer()
  {
    release();
  }

  explicit operator bool() const
  {
    return h_ != nullptr;
  }

  /** True when this is the only handle (acquire: pairs with other handles'
   * release decrements, so their prior reads/writes happen-before ours). */
  bool unique() const
  {
    return h_ && h_->refs.load(std::memory_order_acquire) == 1;
  }

  uint32_t use_count() const
  {
    return h_ ? h_->refs.load(std::memory_order_relaxed) : 0;
  }

  uint32_t capacity() const
  {
    return h_ ? h_->capacity : 0;
  }

  T *data()
  {
    return h_ ? elems() : nullptr;
  }

  const T *data() const
  {
    return h_ ? elems() : nullptr;
  }

  /** Identity of the underlying buffer (for sharing checks). */
  const void *id() const
  {
    return h_;
  }

  void reset()
  {
    release();
  }
};

} // namespace litestl::util
