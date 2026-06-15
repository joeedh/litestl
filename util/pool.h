#pragma once
#include "alloc.h"
#include "boolvector.h"
#include "compiler_util.h"
#include "vector.h"
#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

namespace litestl::util {

/**
 * Typed slab pool with intrusive freelist.
 *
 * Backing storage is fixed-size slabs of SLAB_N objects allocated through
 * alloc::New so the leak tracker sees them. release(p) destroys *p and
 * pushes the slot onto a per-object freelist for reuse. ~Pool walks every
 * slab and destroys any slot still marked live.
 *
 * Move-only. Not thread-safe.
 */
template <typename T, int SLAB_N = 64> class Pool {
  static_assert(SLAB_N > 0, "SLAB_N must be positive");
  static_assert(sizeof(T) >= sizeof(void *),
                "T must be at least pointer-sized for the intrusive freelist");

  struct Slab {
    alignas(T) std::byte data[sizeof(T) * SLAB_N];
    BoolVector<SLAB_N> live;
  };

  struct FreeNode {
    FreeNode *next;
  };

public:
  Pool() = default;

  Pool(const Pool &) = delete;
  Pool &operator=(const Pool &) = delete;

  Pool(Pool &&b) noexcept
  {
    move_from(std::move(b));
  }

  Pool &operator=(Pool &&b) noexcept
  {
    if (this != &b) {
      destroy_all();
      move_from(std::move(b));
    }
    return *this;
  }

  ~Pool()
  {
    destroy_all();
  }

  template <typename... Args> T *alloc(Args &&...args)
  {
    void *slot;
    int slab_idx;
    int slot_in_slab;

    if (freelist_) {
      FreeNode *node = freelist_;
      freelist_ = node->next;
      slot = static_cast<void *>(node);
      locate(slot, slab_idx, slot_in_slab);
    } else {
      if (next_slot_ >= SLAB_N) {
        Slab *s = ::litestl::alloc::New<Slab>("util::Pool<T>::Slab");
        slabs_.append(s);
        next_slot_ = 0;
      }
      slab_idx = slabs_.size() - 1;
      slot_in_slab = next_slot_++;
      slot = static_cast<void *>(&slabs_[slab_idx]->data[sizeof(T) * slot_in_slab]);
    }

    T *p = new (slot) T(std::forward<Args>(args)...);
    slabs_[slab_idx]->live.set(slot_in_slab, true);
    live_++;
    return p;
  }

  bool release(T *p)
  {
    if (!p) {
      return false;
    }
    int slab_idx;
    int slot_in_slab;
    locate(static_cast<void *>(p), slab_idx, slot_in_slab);
    if (slabs_[slab_idx]->live[slot_in_slab]) {
      p->~T();
      slabs_[slab_idx]->live.set(slot_in_slab, false);
      FreeNode *node = static_cast<FreeNode *>(static_cast<void *>(p));
      node->next = freelist_;
      freelist_ = node;
      live_--;
      return true;
    }
    return false;
  }

  void clear()
  {
    destroy_all();
    slabs_.clear();
    freelist_ = nullptr;
    next_slot_ = SLAB_N;
    live_ = 0;
  }

  int live_count() const
  {
    return live_;
  }

  /**
   * Forward iterator over live objects: walks slabs in slot order, skipping
   * freed / never-allocated slots via the per-slab live bitmap. Invalidated by
   * any alloc()/release()/clear() — don't mutate the pool mid-traversal.
   */
  template <bool is_const> struct iterator_base {
    using pool_type = std::conditional_t<is_const, const Pool, Pool>;
    using value_type = std::conditional_t<is_const, const T, T>;

    iterator_base(pool_type *pool, int i) : pool_(pool), i_(i)
    {
      if (i_ == 0) {
        /* Prewind to the first live slot. */
        i_ = -1;
        operator++();
      }
    }

    bool operator==(const iterator_base &b) const
    {
      return i_ == b.i_;
    }
    bool operator!=(const iterator_base &b) const
    {
      return i_ != b.i_;
    }

    value_type &operator*() const
    {
      Slab *s = pool_->slabs_[i_ / SLAB_N];
      return *reinterpret_cast<value_type *>(&s->data[sizeof(T) * (i_ % SLAB_N)]);
    }
    value_type *operator->() const
    {
      return &operator*();
    }

    iterator_base &operator++()
    {
      const int cap = int(pool_->slabs_.size()) * SLAB_N;
      i_++;
      while (i_ < cap && !pool_->slabs_[i_ / SLAB_N]->live[i_ % SLAB_N]) {
        i_++;
      }
      return *this;
    }

  private:
    pool_type *pool_;
    int i_;
  };

  using iterator = iterator_base<false>;
  using const_iterator = iterator_base<true>;

  iterator begin()
  {
    return iterator(this, 0);
  }
  iterator end()
  {
    return iterator(this, int(slabs_.size()) * SLAB_N);
  }
  const_iterator begin() const
  {
    return const_iterator(this, 0);
  }
  const_iterator end() const
  {
    return const_iterator(this, int(slabs_.size()) * SLAB_N);
  }

  size_t capacity() const { return slabs_.size() * SLAB_N; }

private:
  void locate(void *slot, int &slab_idx, int &slot_in_slab)
  {
    auto *byte_slot = static_cast<std::byte *>(slot);
    for (int i = 0; i < slabs_.size(); i++) {
      std::byte *begin = slabs_[i]->data;
      std::byte *end = begin + sizeof(T) * SLAB_N;
      if (byte_slot >= begin && byte_slot < end) {
        slab_idx = i;
        slot_in_slab = static_cast<int>((byte_slot - begin) / sizeof(T));
        return;
      }
    }
    fprintf(stderr, "util::Pool::locate: pointer %p not owned by this pool\n", slot);
    abort();
  }

  void destroy_all()
  {
    for (int i = 0; i < slabs_.size(); i++) {
      Slab *s = slabs_[i];
      const int limit = (i == slabs_.size() - 1) ? next_slot_ : SLAB_N;
      for (int j = 0; j < limit; j++) {
        if (s->live[j]) {
          T *p = reinterpret_cast<T *>(&s->data[sizeof(T) * j]);
          p->~T();
        }
      }
      ::litestl::alloc::Delete(s);
    }
    slabs_.clear();
    freelist_ = nullptr;
    next_slot_ = SLAB_N;
    live_ = 0;
  }

  void move_from(Pool &&b) noexcept
  {
    slabs_ = std::move(b.slabs_);
    freelist_ = b.freelist_;
    next_slot_ = b.next_slot_;
    live_ = b.live_;
    b.freelist_ = nullptr;
    b.next_slot_ = SLAB_N;
    b.live_ = 0;
  }

  Vector<Slab *, 1> slabs_;
  FreeNode *freelist_ = nullptr;
  int next_slot_ = SLAB_N; // forces a fresh slab on first alloc
  int live_ = 0;
};

} // namespace litestl::util
