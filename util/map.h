#pragma once

/*
 * Open-addressing hash map (Swiss-table-lite).
 *
 * Power-of-2 table; the bucket is mixHash(hash(key)) & (cap-1) (mixHash
 * de-clusters identity-hashed ints so masking distributes well). Each slot has a
 * control byte in a separate contiguous array: kEmpty, kDeleted (tombstone), or
 * FULL with a 7-bit hash fragment (h2). Lookups scan kGroupWidth (8) control
 * bytes at a time via portable uint64 SWAR (hash::swiss): the h2 filter rejects
 * non-matching FULL slots without touching the Pair array, and matchEmpty ends a
 * negative lookup in one group test. Probing advances by triangular multiples of
 * the group width, which over a power-of-2 table visits every group exactly once.
 *
 * Deletion uses tombstones: a kDeleted slot is NOT end-of-chain (a key inserted
 * before the deletion may live further along), so a probe only stops on kEmpty;
 * key equality is tested only on FULL slots. erase() turns a slot kEmpty when its
 * group still has an empty (it bridges no chain), else kDeleted; tombstones are
 * reclaimed by a same-size rehash once the table fills. Load factor is 7/8.
 *
 * size() is exact (used_count_ tracks live entries; deleted_ tracks tombstones).
 * Small-buffer optimized: cap up to static_cap_v lives inline (pairs + control
 * bytes), heap otherwise.
 */

#include "alloc.h"
#include "compiler_util.h"
#include "concepts.h"
#include "hash.h"

#include <bit>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <span>
#include <type_traits>

namespace litestl::util {

namespace detail::map {

/** Concept for callables that copy or transform a key during map insertion. */
template <typename Func, typename Key>
/* clang-format off */
concept KeyCopier = requires(Func f, Key k)
{
  {f(k)} -> IsAnyOf<Key, Key&, Key&&, const Key&>;
};
/* clang-format on */

/** Smallest power of two >= n, but never below floor_. */
constexpr size_t pow2AtLeast(size_t n, size_t floor_)
{
  size_t v = std::bit_ceil(n < 1 ? size_t(1) : n);
  return v < floor_ ? floor_ : v;
}

template <typename Key, typename Value> struct Pair {
  Key key;
  Value value;

  Pair()
  {
  }

  Pair(Pair &&b) = default;
  Pair(const Pair &b) = default;

  Pair(Key &key_, Value &value_)
  {
    key = key_;
    value = value_;
  }
  Pair(Key &&key_, Value &&value_)
  {
    key = key_;
    value = value_;
  }

  Pair &operator=(Pair &&b)
  {
    if (this == &b) {
      return *this;
    }

    key = std::move(b.key);
    value = std::move(b.value);

    return *this;
  }

  Pair &operator=(const Pair &b)
  {
    if (this == &b) {
      return *this;
    }

    key = b.key;
    value = b.value;

    return *this;
  }

  static constexpr bool is_simple()
  {
    return (std::is_integral_v<Key> || std::is_pointer_v<Key>) &&
           (std::is_integral_v<Value> || std::is_floating_point_v<Value> ||
            std::is_pointer_v<Value>);
  }
};
} // namespace detail::map

/**
 * Open-addressing hash map; see the file header comment for the control-byte /
 * SWAR probing strategy and tombstone semantics.
 *
 * Stores up to roughly @p static_size key-value pairs inline (the inline table
 * capacity is a power of two >= static_size*3+1). Falls back to heap via
 * alloc::alloc beyond that. Rehashes at a 7/8 load factor; uses tombstones for
 * deletion.
 */
template <typename Key, typename Value, int static_size = 16>
class alignas(ContainerAlign<detail::map::Pair<Key, Value>>()) Map {
  using Pair = detail::map::Pair<Key, Value>;

  static constexpr int kGroupWidth = hash::swiss::kGroupWidth;
  /* Power-of-2 inline capacity, at least one group wide. */
  static constexpr int static_cap_v =
      int(detail::map::pow2AtLeast(size_t(static_size) * 3 + 1, size_t(kGroupWidth)));
  /* Load factor = kLoadNum / kLoadDen. Tuned empirically: at scale a denser
   * table wins on cache locality (Swiss control-byte scan keeps probe lengths
   * flat regardless of density), so 7/8 beats 3/4 and 1/2 on lookups + churn. */
  static constexpr size_t kLoadNum = 7, kLoadDen = 8;

public:
  using key_type = Key;
  using value_type = Value;

  struct iterator {
    iterator(const Map *map, int i) : i_(i), map_(map)
    {
      if (i_ == 0) {
        /* Find first item. */
        i_ = -1;
        operator++();
      }
    }

    iterator(const iterator &b) : i_(b.i_), map_(b.map_)
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

    const Pair &operator*() const
    {
      return map_->table_[i_];
    }

    iterator &operator++()
    {
      i_++;

      while (i_ < int(map_->table_.size()) &&
             !hash::swiss::ctrlIsFull(map_->ctrl_[i_])) {
        i_++;
      }

      return *this;
    }

  private:
    int i_;
    const Map *map_;
  };

  template <bool is_key, typename T> struct key_value_range {
    key_value_range(const Map *map, int i = 0) : map_(map), i_(i)
    {
      if (i == 0) {
        i_--;
        operator++();
      }
    }

    key_value_range(const key_value_range &b) : map_(b.map_), i_(b.i_)
    {
    }

    bool operator==(const key_value_range &b)
    {
      return b.i_ == i_;
    }

    bool operator!=(const key_value_range &b)
    {
      return !operator==(b);
    }

    T &operator*()
    {
      if constexpr (is_key) {
        return map_->table_[i_].key;
      } else {
        return map_->table_[i_].value;
      }
    }

    key_value_range &operator++()
    {
      i_++;

      while (i_ < int(map_->table_.size()) &&
             !hash::swiss::ctrlIsFull(map_->ctrl_[i_])) {
        i_++;
      }

      return *this;
    }

    key_value_range begin() const
    {
      return key_value_range(map_, 0);
    }

    key_value_range end() const
    {
      return key_value_range(map_, int(map_->table_.size()));
    }

  private:
    const Map *map_;
    int i_ = 0;
  };

  using key_range = key_value_range<true, Key>;
  using value_range = key_value_range<false, Value>;

  friend struct key_value_range<true, Key>;

  Map()
  {
    init_inline();
  }

  Map(const Map &b)
  {
    size_t cap = b.table_.size();

    if (cap <= size_t(static_cap_v)) {
      table_ = std::span(get_static(), cap);
      ctrl_ = static_ctrl_;
    } else {
      table_ = std::span(
          static_cast<Pair *>(alloc::alloc("copied map table", sizeof(Pair) * cap)),
          cap);
      ctrl_ = static_cast<uint8_t *>(alloc::alloc("copied map ctrl", cap + kGroupWidth));
    }

    std::memcpy(ctrl_, b.ctrl_, cap + kGroupWidth);
    used_count_ = b.used_count_;
    deleted_ = b.deleted_;

    if constexpr (Pair::is_simple()) {
      std::memcpy(static_cast<void *>(table_.data()),
                  static_cast<const void *>(b.table_.data()), sizeof(Pair) * cap);
    } else {
      for (size_t i = 0; i < cap; i++) {
        if (hash::swiss::ctrlIsFull(ctrl_[i])) {
          /* table_ is raw storage; copy-construct rather than assign. */
          new (static_cast<void *>(&table_[i])) Pair(b.table_[i]);
        }
      }
    }
  }

  Map(Map &&b) : used_count_(b.used_count_), deleted_(b.deleted_)
  {
    if (b.using_heap()) {
      /* Steal the heap buffers outright. */
      table_ = b.table_;
      ctrl_ = b.ctrl_;
    } else {
      /* b's table lives inline; move the live pairs into our own storage. */
      size_t cap = b.table_.size();
      table_ = std::span(get_static(), cap);
      ctrl_ = static_ctrl_;
      std::memcpy(ctrl_, b.ctrl_, cap + kGroupWidth);

      if constexpr (Pair::is_simple()) {
        std::memcpy(static_cast<void *>(get_static()),
                    static_cast<const void *>(b.table_.data()), sizeof(Pair) * cap);
      } else {
        for (size_t i = 0; i < cap; i++) {
          if (hash::swiss::ctrlIsFull(ctrl_[i])) {
            new (static_cast<void *>(&table_[i])) Pair(std::move(b.table_[i]));
            b.table_[i].~Pair();
          }
        }
      }
    }

    /* Reset b to a valid empty inline state so its destructor neither frees the
     * buffers we now own nor touches the pairs we moved out. */
    b.table_ = std::span(b.get_static(), size_t(0));
    b.ctrl_ = b.static_ctrl_;
    b.used_count_ = 0;
    b.deleted_ = 0;
  }

  DEFAULT_COPY_ASSIGNMENT(Map)
  DEFAULT_MOVE_ASSIGNMENT(Map)

  Map(std::initializer_list<Pair> list)
  {
    init_inline();
    reserve(list.size());

    for (auto &&item : list) {
      add_overwrite(item.key, item.value);
    }
  }

  ~Map()
  {
    if constexpr (!Pair::is_simple()) {
      for (size_t i = 0; i < table_.size(); i++) {
        if (hash::swiss::ctrlIsFull(ctrl_[i])) {
          table_[i].~Pair();
        }
      }
    }

    if (using_heap()) {
      alloc::release(static_cast<void *>(table_.data()));
      alloc::release(static_cast<void *>(ctrl_));
    }
  }

  /** Returns an iterable range over all keys in the map. */
  key_range keys() const
  {
    return key_range(this);
  }

  /** Returns an iterable range over all values in the map. */
  value_range values() const
  {
    return value_range(this);
  }

  iterator begin() const
  {
    return iterator(this, 0);
  }

  iterator end() const
  {
    return iterator(this, int(table_.size()));
  }

  /** Returns the number of entries currently in the map. */
  size_t size() const
  {
    return size_t(used_count_);
  }

  /**
   * Inserts @p key and @p value without checking for duplicates. Caller must
   * ensure @p key is not already present; otherwise the map will contain
   * duplicate entries.
   */
  void insert(const Key &key, const Value &value)
  {
    reserve_for_insert();
    hash::HashInt mixed = hash::mixHash(hash::hash(key));
    finalize_insert(prepare_insert(mixed), key, value, hash::swiss::h2(mixed));
  }

  /** Rvalue overload of insert method above.  Does not check if key already exists.*/
  void insert(Key &&key, Value &&value)
  {
    reserve_for_insert();
    hash::HashInt mixed = hash::mixHash(hash::hash(key));
    finalize_insert(prepare_insert(mixed), key, value, hash::swiss::h2(mixed));
  }

  /**
   * Inserts @p key and @p value if @p key is not already present. Returns true
   * if inserted, false if the key already existed (value is not overwritten).
   */
  bool add(const Key &key, const Value &value)
  {
    return add_intern<false>(key, value);
  }
  bool add(const Key &&key, const Value &&value)
  {
    return add_intern<false>(key, value);
  }

  /**
   * Inserts or overwrites. Returns true if @p key was new, false if an
   * existing value was overwritten.
   */
  bool add_overwrite(const Key &key, const Value &value)
  {
    return add_intern<true>(key, value);
  }
  bool add_overwrite(const Key &&key, const Value &&value)
  {
    return add_intern<true>(key, value);
  }

  /**
   * Returns a reference to the value for @p key, inserting a
   * default-constructed value if the key is not present.
   */
  Value &operator[](const Key &key)
  {
    reserve_for_insert();
    hash::HashInt mixed = hash::mixHash(hash::hash(key));
    FindResult fr = find_or_prepare_insert(key, mixed);

    if (!fr.found) {
      bool was_empty = ctrl_[fr.index] == hash::swiss::kEmpty;

      /* Simple values must be explicitly zeroed — the slot is raw storage. */
      if constexpr (!is_simple<Value>()) {
        new (static_cast<void *>(&table_[fr.index].value)) Value();
      } else {
        table_[fr.index].value = Value();
      }

      new (static_cast<void *>(&table_[fr.index].key)) Key(key);
      set_ctrl(fr.index, hash::swiss::h2(mixed));
      used_count_++;
      if (!was_empty) {
        deleted_--;
      }
    }
    return table_[fr.index].value;
  }

  /** Returns true if @p key is present in the map. */
  bool contains(const Key &key) const
  {
    return find_index(key) != -1;
  }
  bool contains(const Key &&key) const
  {
    return find_index(key) != -1;
  }

  /** Returns a pointer to the value for @p key, or nullptr if not found. */
  Value *lookup_ptr(const Key &key)
  {
    int i = find_index(key);
    if (i < 0) {
      return nullptr;
    }

    return &table_[i].value;
  }

  /**
   * Inserts @p key only if absent, using @p copy_key to construct the stored
   * key and @p set_value to construct the stored value. Returns a reference to
   * the value (existing or newly created). Does nothing if key already exists.
   */
  template <detail::map::KeyCopier<Key> KeyCopyFunc, typename ValueSetFunc>
  Value &add_callback(const Key &key, KeyCopyFunc copy_key, ValueSetFunc set_value)
  {
    reserve_for_insert();
    hash::HashInt mixed = hash::mixHash(hash::hash(key));
    FindResult fr = find_or_prepare_insert(key, mixed);

    if (!fr.found) {
      bool was_empty = ctrl_[fr.index] == hash::swiss::kEmpty;

      /* Use copy/move constructors since we have unallocated memory. */
      new (static_cast<void *>(&table_[fr.index].key)) Key(copy_key(key));
      new (static_cast<void *>(&table_[fr.index].value)) Value(set_value());
      set_ctrl(fr.index, hash::swiss::h2(mixed));
      used_count_++;
      if (!was_empty) {
        deleted_--;
      }
    }

    return table_[fr.index].value;
  }

  /**
   * Inserts @p key if absent and writes a pointer to the value slot into
   * @p value. The value slot is default-initialized for non-trivial types
   * so the caller can use assignment rather than placement new. Returns true
   * if the key was newly inserted.
   */
  bool add_uninitialized(const Key &key, Value **value)
  {
    reserve_for_insert();
    hash::HashInt mixed = hash::mixHash(hash::hash(key));
    FindResult fr = find_or_prepare_insert(key, mixed);

    if (value) {
      *value = &table_[fr.index].value;
    }

    if (!fr.found) {
      bool was_empty = ctrl_[fr.index] == hash::swiss::kEmpty;

      // make life easier to client code by
      // default initializing the value, which allows them to
      // use assignment operator instead of placement new.
      if (!is_simple<Value>()) {
        new (static_cast<void *>(&table_[fr.index].value)) Value();
      }

      // use placement new instead of assignment
      new (static_cast<void *>(&table_[fr.index].key)) Key(key);
      set_ctrl(fr.index, hash::swiss::h2(mixed));
      used_count_++;
      if (!was_empty) {
        deleted_--;
      }
      return true;
    }

    return false;
  }

  /**
   * Returns a reference to the value for @p key. Undefined behavior if
   * @p key is not in the map; check with contains() first.
   */
  Value &lookup(const Key &key)
  {
    return table_[find_index(key)].value;
  }

  const Value &lookup(const Key &key) const
  {
    return table_[find_index(key)].value;
  }

  /**
   * Removes @p key from the map. If @p out_value is non-null, the removed
   * value is moved into it. Returns true if the key was found and removed.
   */
  bool remove(const Key &key, Value *out_value = nullptr)
  {
    int i = find_index(key);

    if (i == -1) {
      return false;
    }

    if (out_value) {
      *out_value = std::move(table_[i].value);
    }

    if constexpr (!Pair::is_simple()) {
      table_[i].~Pair();
    }

    erase_at(size_t(i));
    used_count_--;

    return true;
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
        hash::HashInt mixed = hash::mixHash(hash::hash(table_[i].key));
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
  std::span<Pair> table_;
  uint8_t *ctrl_ = nullptr;
  /* Align the inline storage to the same value the struct itself requests
   * (ContainerAlign<Pair>) — a hardcoded alignas(8) would exceed the struct's
   * alignment for small pairs on wasm (sizeof(void*)==4) and fail to compile. */
  alignas(ContainerAlign<Pair>()) char static_storage_[static_cap_v * sizeof(Pair)];
  alignas(ContainerAlign<Pair>()) uint8_t static_ctrl_[static_cap_v + kGroupWidth];
  uint32_t used_count_ = 0;
  uint32_t deleted_ = 0;

  struct FindResult {
    int index;
    bool found;
  };

  Pair *get_static()
  {
    return reinterpret_cast<Pair *>(static_storage_);
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
        if (table_[idx].key == key) {
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
   * assumed absent (used by insert() and rehash). */
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
        if (table_[idx].key == key) {
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

  template <bool overwrite> bool add_intern(const Key &key, const Value &value)
  {
    reserve_for_insert();
    hash::HashInt mixed = hash::mixHash(hash::hash(key));
    FindResult fr = find_or_prepare_insert(key, mixed);

    if (fr.found) {
      if constexpr (overwrite) {
        table_[fr.index].value = value;
      }
      return false;
    }

    finalize_insert(fr.index, key, value, hash::swiss::h2(mixed));
    return true;
  }

  /** Constructs a new entry into a known-free slot (@p key assumed absent). */
  void finalize_insert(int slot, const Key &key, const Value &value, uint8_t h2b)
  {
    bool was_empty = ctrl_[slot] == hash::swiss::kEmpty;

    if constexpr (!Pair::is_simple()) {
      new (static_cast<void *>(&table_[slot].key)) Key(key);
      new (static_cast<void *>(&table_[slot].value)) Value(value);
    } else {
      table_[slot].key = key;
      table_[slot].value = value;
    }

    set_ctrl(slot, h2b);
    used_count_++;
    if (!was_empty) {
      deleted_--;
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
   * live entries, dropping tombstones. Always heap-allocates so an inline table
   * never aliases itself during the copy. */
  void resize(size_t new_cap)
  {
    Pair *old_pairs = table_.data();
    uint8_t *old_ctrl = ctrl_;
    size_t old_cap = table_.size();
    bool old_heap = using_heap();

    table_ = std::span(
        static_cast<Pair *>(alloc::alloc("util::map table", new_cap * sizeof(Pair))),
        new_cap);
    ctrl_ = static_cast<uint8_t *>(alloc::alloc("util::map ctrl", new_cap + kGroupWidth));
    std::memset(ctrl_, hash::swiss::kEmpty, new_cap + kGroupWidth);
    used_count_ = 0;
    deleted_ = 0;

    for (size_t i = 0; i < old_cap; i++) {
      if (!hash::swiss::ctrlIsFull(old_ctrl[i])) {
        continue;
      }

      hash::HashInt mixed = hash::mixHash(hash::hash(old_pairs[i].key));
      int slot = prepare_insert(mixed);

      if constexpr (Pair::is_simple()) {
        table_[slot] = old_pairs[i];
      } else {
        new (static_cast<void *>(&table_[slot].key)) Key(std::move(old_pairs[i].key));
        new (static_cast<void *>(&table_[slot].value)) Value(std::move(old_pairs[i].value));
        old_pairs[i].~Pair();
      }

      set_ctrl(slot, hash::swiss::h2(mixed));
      used_count_++;
    }

    if (old_heap) {
      alloc::release(static_cast<void *>(old_pairs));
      alloc::release(static_cast<void *>(old_ctrl));
    }
  }

public:
  /* Remove all entries but keep the allocated table (capacity retained), so a
   * clear + refill reuses storage instead of reallocating. */
  inline Map &clear()
  {
    if constexpr (!Pair::is_simple()) {
      for (size_t i = 0; i < table_.size(); i++) {
        if (hash::swiss::ctrlIsFull(ctrl_[i])) {
          table_[i].~Pair();
        }
      }
    }

    std::memset(ctrl_, hash::swiss::kEmpty, table_.size() + kGroupWidth);
    used_count_ = 0;
    deleted_ = 0;
    return *this;
  }
};
} // namespace litestl::util
