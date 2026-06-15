#pragma once

/*
 * Open-addressing hash set (Swiss-table-lite). The single-key counterpart to
 * util::Map; see map.h's header comment for the full design.
 *
 * Power-of-2 table; the bucket is mixHash(hash(key)) & (cap-1). Each slot has a
 * control byte in a separate contiguous array: kEmpty, kDeleted (tombstone), or
 * FULL with a 7-bit hash fragment (h2). Lookups scan kGroupWidth (8) control
 * bytes at a time via portable uint64 SWAR (hash::swiss): the h2 filter rejects
 * non-matching FULL slots without touching the key array, and matchEmpty ends a
 * negative lookup in one group test. Probing advances by triangular multiples of
 * the group width, which over a power-of-2 table visits every group exactly once.
 *
 * Deletion uses tombstones: a kDeleted slot is NOT end-of-chain, so a probe only
 * stops on kEmpty; key equality is tested only on FULL slots. erase() turns a
 * slot kEmpty when its group still has an empty, else kDeleted; tombstones are
 * reclaimed by a same-size rehash once the table fills. Load factor is 7/8.
 *
 * size() is exact. Small-buffer optimized: cap up to static_cap_v lives inline
 * (keys + control bytes), heap otherwise.
 */

#include "alloc.h"
#include "compiler_util.h"
#include "hash.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <span>

namespace litestl::util {

namespace detail::set {
/** Smallest power of two >= n, but never below floor_. */
constexpr size_t pow2AtLeast(size_t n, size_t floor_)
{
  size_t v = std::bit_ceil(n < 1 ? size_t(1) : n);
  return v < floor_ ? floor_ : v;
}
} // namespace detail::set

/**
 * Open-addressing hash set; see the file header comment for the control-byte /
 * SWAR probing strategy and tombstone semantics.
 *
 * Stores up to roughly @p static_size_logical keys inline (the inline table
 * capacity is a power of two >= static_size_logical*3+1). Falls back to heap via
 * alloc::alloc beyond that. Rehashes at a 7/8 load factor; uses tombstones for
 * deletion.
 */
// cannot rely on pointer members forcibly aligning to 8
// because of wasm
template <typename Key, size_t static_size_logical = 4>
struct alignas(ContainerAlign<Key>()) Set {
  using key_type = Key;

private:
  static constexpr int kGroupWidth = hash::swiss::kGroupWidth;
  /* Power-of-2 inline capacity, at least one group wide. */
  static constexpr int static_cap_v =
      int(detail::set::pow2AtLeast(static_size_logical * 3 + 1, size_t(kGroupWidth)));
  /* Load factor = kLoadNum / kLoadDen (tuned with Map; 7/8 wins at scale). */
  static constexpr size_t kLoadNum = 7, kLoadDen = 8;

public:
  struct iterator {
    using key_type = Key;

    iterator(const Set *set, int i) : set_(set), i_(i)
    {
      if (i == 0) {
        /* Prewind iterator to first key. */
        i_--;
        operator++();
      }
    }
    iterator(const iterator &b) : set_(b.set_), i_(b.i_)
    {
    }

    bool operator==(const iterator &b) const
    {
      return b.i_ == i_;
    }
    bool operator!=(const iterator &b) const
    {
      return !operator==(b);
    }

    const Key &operator*() const
    {
      return set_->table_[i_];
    }

    iterator &operator++()
    {
      i_++;

      while (i_ < int(set_->table_.size()) &&
             !hash::swiss::ctrlIsFull(set_->ctrl_[i_])) {
        i_++;
      }

      return *this;
    }

  private:
    const Set *set_;
    int i_;
  };

  Set()
  {
    init_inline();
  }

  Set(Set &&b) : used_count_(b.used_count_), deleted_(b.deleted_)
  {
    if (b.using_heap()) {
      /* Steal the heap buffers outright. */
      table_ = b.table_;
      ctrl_ = b.ctrl_;
    } else {
      /* b's table lives inline; move the live keys into our own storage. */
      size_t cap = b.table_.size();
      table_ = std::span(get_static(), cap);
      ctrl_ = static_ctrl_;
      std::memcpy(ctrl_, b.ctrl_, cap + kGroupWidth);

      if constexpr (is_simple<Key>()) {
        std::memcpy(static_cast<void *>(get_static()),
                    static_cast<const void *>(b.table_.data()), sizeof(Key) * cap);
      } else {
        for (size_t i = 0; i < cap; i++) {
          if (hash::swiss::ctrlIsFull(ctrl_[i])) {
            new (static_cast<void *>(&table_[i])) Key(std::move(b.table_[i]));
            b.table_[i].~Key();
          }
        }
      }
    }

    /* Reset b to a valid empty inline state. */
    b.table_ = std::span(b.get_static(), size_t(0));
    b.ctrl_ = b.static_ctrl_;
    b.used_count_ = 0;
    b.deleted_ = 0;
  }

  Set(const Set &b)
  {
    size_t cap = b.table_.size();

    if (cap <= size_t(static_cap_v)) {
      table_ = std::span(get_static(), cap);
      ctrl_ = static_ctrl_;
    } else {
      table_ = std::span(
          static_cast<Key *>(alloc::alloc("copied set table", sizeof(Key) * cap)), cap);
      ctrl_ = static_cast<uint8_t *>(alloc::alloc("copied set ctrl", cap + kGroupWidth));
    }

    std::memcpy(ctrl_, b.ctrl_, cap + kGroupWidth);
    used_count_ = b.used_count_;
    deleted_ = b.deleted_;

    if constexpr (is_simple<Key>()) {
      std::memcpy(static_cast<void *>(table_.data()),
                  static_cast<const void *>(b.table_.data()), sizeof(Key) * cap);
    } else {
      for (size_t i = 0; i < cap; i++) {
        if (hash::swiss::ctrlIsFull(ctrl_[i])) {
          new (static_cast<void *>(&table_[i])) Key(b.table_[i]);
        }
      }
    }
  }

  ~Set()
  {
    if constexpr (!is_simple<Key>()) {
      for (size_t i = 0; i < table_.size(); i++) {
        if (hash::swiss::ctrlIsFull(ctrl_[i])) {
          table_[i].~Key();
        }
      }
    }

    if (using_heap()) {
      alloc::release(static_cast<void *>(table_.data()));
      alloc::release(static_cast<void *>(ctrl_));
    }
  }

  DEFAULT_MOVE_ASSIGNMENT(Set)
  DEFAULT_COPY_ASSIGNMENT(Set)

  iterator begin() const
  {
    return iterator(this, 0);
  }
  iterator end() const
  {
    return iterator(this, int(table_.size()));
  }

  /** Inserts @p key if not already present. Returns true if inserted, false
   * if the key already existed. */
  bool add(const Key &key)
  {
    reserve_for_insert();
    hash::HashInt mixed = hash::mixHash(hash::hash(key));
    FindResult fr = find_or_prepare_insert(key, mixed);

    if (fr.found) {
      return false;
    }

    bool was_empty = ctrl_[fr.index] == hash::swiss::kEmpty;

    if constexpr (is_simple<Key>()) {
      table_[fr.index] = key;
    } else {
      new (static_cast<void *>(&table_[fr.index])) Key(key);
    }

    set_ctrl(fr.index, hash::swiss::h2(mixed));
    used_count_++;
    if (!was_empty) {
      deleted_--;
    }

    return true;
  }

  /** Removes @p key from the set. Returns true if the key was found and removed. */
  bool remove(const Key &key)
  {
    int i = find_index(key);

    if (i == -1) {
      return false;
    }

    if constexpr (!is_simple<Key>()) {
      table_[i].~Key();
    }

    erase_at(size_t(i));
    used_count_--;

    return true;
  }

  /** Returns true if @p key is present in the set. */
  bool contains(const Key &key) const
  {
    return find_index(key) != -1;
  }

  /** Alias for contains(). */
  bool operator[](const Key &key) const
  {
    return contains(key);
  }

  /** Returns the number of entries currently in the set. */
  size_t size() const
  {
    return size_t(used_count_);
  }

  /**
   * Pre-allocates table capacity for at least @p size entries without
   * changing the current contents.
   */
  void reserve(size_t size)
  {
    size_t need = pow2_cap((size * kLoadDen) / kLoadNum + 1);

    if (table_.size() >= need) {
      return;
    }

    resize(need);
  }

  /** Removes all entries from the set, destructing non-trivial keys. */
  Set &clear()
  {
    if constexpr (!is_simple<Key>()) {
      for (size_t i = 0; i < table_.size(); i++) {
        if (hash::swiss::ctrlIsFull(ctrl_[i])) {
          table_[i].~Key();
        }
      }
    }

    std::memset(ctrl_, hash::swiss::kEmpty, table_.size() + kGroupWidth);
    used_count_ = 0;
    deleted_ = 0;
    return *this;
  }

  bool using_heap() const
  {
    return static_cast<const void *>(table_.data()) !=
           static_cast<const void *>(static_storage_);
  }

  /* Test/debug-only invariant check (not gated on NDEBUG so it survives the
   * RelWithDebInfo test build; as a template member it costs nothing unless
   * called): every FULL slot's control byte equals h2(mixHash(hash(key))); the
   * iterated live count matches used_count_; the cloned tail mirrors the head. */
  bool debugCheckInvariants() const
  {
    const size_t cap = table_.size();
    size_t live = 0;
    for (size_t i = 0; i < cap; i++) {
      if (hash::swiss::ctrlIsFull(ctrl_[i])) {
        live++;
        hash::HashInt mixed = hash::mixHash(hash::hash(table_[i]));
        if (ctrl_[i] != hash::swiss::h2(mixed)) {
          return false;
        }
      }
    }
    if (live != size_t(used_count_)) {
      return false;
    }
    for (int i = 0; i < kGroupWidth - 1; i++) {
      if (ctrl_[cap + i] != ctrl_[i]) {
        return false;
      }
    }
    return true;
  }

private:
  std::span<Key> table_;
  uint8_t *ctrl_ = nullptr;
  /* Align the inline storage to the same value the struct itself requests
   * (ContainerAlign<Key>) — a hardcoded alignas(8) would exceed the struct's
   * alignment for small keys on wasm (sizeof(void*)==4) and fail to compile. */
  alignas(ContainerAlign<Key>()) char static_storage_[static_cap_v * sizeof(Key)];
  alignas(ContainerAlign<Key>()) uint8_t static_ctrl_[static_cap_v + kGroupWidth];
  uint32_t used_count_ = 0;
  uint32_t deleted_ = 0;

  struct FindResult {
    int index;
    bool found;
  };

  Key *get_static()
  {
    return reinterpret_cast<Key *>(static_storage_);
  }

  /** Largest live entry count allowed at capacity @p cap before a rehash. */
  static size_t max_load(size_t cap)
  {
    return cap / kLoadDen * kLoadNum;
  }

  /** Smallest power of two >= size, at least one group wide. */
  static size_t pow2_cap(size_t size)
  {
    return std::bit_ceil(size < size_t(kGroupWidth) ? size_t(kGroupWidth) : size);
  }

  void init_inline()
  {
    table_ = std::span(get_static(), size_t(static_cap_v));
    ctrl_ = static_ctrl_;
    std::memset(ctrl_, hash::swiss::kEmpty, size_t(static_cap_v) + kGroupWidth);
    used_count_ = 0;
    deleted_ = 0;
  }

  /** Writes a control byte, mirroring into the cloned tail for wrap-around
   * group loads. */
  void set_ctrl(size_t i, uint8_t c)
  {
    ctrl_[i] = c;
    if (i < size_t(kGroupWidth - 1)) {
      ctrl_[table_.size() + i] = c;
    }
  }

  /** Index of @p key, or -1. Key equality is tested only on FULL slots whose
   * h2 fragment matches. */
  int find_index(const Key &key) const
  {
    const size_t mask = table_.size() - 1;
    const hash::HashInt mixed = hash::mixHash(hash::hash(key));
    const uint8_t h2b = hash::swiss::h2(mixed);
    size_t pos = size_t(mixed) & mask;
    size_t stride = 0;

    while (true) {
      hash::swiss::Group g(ctrl_ + pos);
      for (uint64_t m = g.match(h2b); m; m &= m - 1) {
        size_t idx = (pos + hash::swiss::lowestLane(m)) & mask;
        if (table_[idx] == key) {
          return int(idx);
        }
      }
      if (g.matchEmpty()) {
        return -1;
      }
      stride += kGroupWidth;
      pos = (pos + stride) & mask;
    }
  }

  /** First free slot (empty or deleted) in @p key's probe order; @p key is
   * assumed absent (used by rehash). */
  int prepare_insert(hash::HashInt mixed) const
  {
    const size_t mask = table_.size() - 1;
    size_t pos = size_t(mixed) & mask;
    size_t stride = 0;

    while (true) {
      hash::swiss::Group g(ctrl_ + pos);
      uint64_t mf = g.matchFree();
      if (mf) {
        return int((pos + hash::swiss::lowestLane(mf)) & mask);
      }
      stride += kGroupWidth;
      pos = (pos + stride) & mask;
    }
  }

  /** Finds @p key; if absent, returns the first free slot in its probe order
   * (reusing a tombstone if one precedes the terminating empty). */
  FindResult find_or_prepare_insert(const Key &key, hash::HashInt mixed)
  {
    const size_t mask = table_.size() - 1;
    const uint8_t h2b = hash::swiss::h2(mixed);
    size_t pos = size_t(mixed) & mask;
    size_t stride = 0;
    int insert_slot = -1;

    while (true) {
      hash::swiss::Group g(ctrl_ + pos);
      for (uint64_t m = g.match(h2b); m; m &= m - 1) {
        size_t idx = (pos + hash::swiss::lowestLane(m)) & mask;
        if (table_[idx] == key) {
          return {int(idx), true};
        }
      }
      if (insert_slot == -1) {
        uint64_t mf = g.matchFree();
        if (mf) {
          insert_slot = int((pos + hash::swiss::lowestLane(mf)) & mask);
        }
      }
      if (g.matchEmpty()) {
        return {insert_slot, false};
      }
      stride += kGroupWidth;
      pos = (pos + stride) & mask;
    }
  }

  /** Clears a removed slot's control byte: kEmpty when its group still has an
   * empty (it bridges no probe chain), else kDeleted. */
  void erase_at(size_t index)
  {
    if constexpr (std::endian::native == std::endian::little) {
      const size_t mask = table_.size() - 1;
      size_t index_before = (index - kGroupWidth) & mask;
      uint64_t empty_after = hash::swiss::Group(ctrl_ + index).matchEmpty();
      uint64_t empty_before = hash::swiss::Group(ctrl_ + index_before).matchEmpty();

      bool was_never_full =
          empty_before && empty_after &&
          (size_t(std::countr_zero(empty_after) >> 3) +
               size_t(std::countl_zero(empty_before) >> 3) <
           size_t(kGroupWidth));

      if (was_never_full) {
        set_ctrl(index, hash::swiss::kEmpty);
        return;
      }
    }

    set_ctrl(index, hash::swiss::kDeleted);
    deleted_++;
  }

  /** Ensures room for one more insert, rehashing if at the load limit. */
  void reserve_for_insert()
  {
    if (size_t(used_count_) + size_t(deleted_) >= max_load(table_.size())) {
      size_t cap = table_.size();
      /* Reclaim tombstones in place when live entries are sparse; otherwise the
       * table is genuinely full — grow. */
      if (size_t(used_count_) < max_load(cap) / 2) {
        resize(cap);
      } else {
        resize(cap * 2);
      }
    }
  }

  /** Reallocates to @p new_cap (power of two) on the heap and re-inserts the
   * live keys, dropping tombstones. Always heap-allocates so an inline table
   * never aliases itself during the copy. */
  void resize(size_t new_cap)
  {
    Key *old_keys = table_.data();
    uint8_t *old_ctrl = ctrl_;
    size_t old_cap = table_.size();
    bool old_heap = using_heap();

    table_ = std::span(
        static_cast<Key *>(alloc::alloc("util::set table", new_cap * sizeof(Key))),
        new_cap);
    ctrl_ = static_cast<uint8_t *>(alloc::alloc("util::set ctrl", new_cap + kGroupWidth));
    std::memset(ctrl_, hash::swiss::kEmpty, new_cap + kGroupWidth);
    used_count_ = 0;
    deleted_ = 0;

    for (size_t i = 0; i < old_cap; i++) {
      if (!hash::swiss::ctrlIsFull(old_ctrl[i])) {
        continue;
      }

      hash::HashInt mixed = hash::mixHash(hash::hash(old_keys[i]));
      int slot = prepare_insert(mixed);

      if constexpr (is_simple<Key>()) {
        table_[slot] = old_keys[i];
      } else {
        new (static_cast<void *>(&table_[slot])) Key(std::move(old_keys[i]));
        old_keys[i].~Key();
      }

      set_ctrl(slot, hash::swiss::h2(mixed));
      used_count_++;
    }

    if (old_heap) {
      alloc::release(static_cast<void *>(old_keys));
      alloc::release(static_cast<void *>(old_ctrl));
    }
  }
};
} // namespace litestl::util
