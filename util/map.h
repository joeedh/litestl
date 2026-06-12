#pragma once

/*
 * Open-addressing hash map.
 *
 * Probing: linear, stride 1. find_pair() starts at hash(key) % size and walks
 * h, h+1, h+2, ... (wrapping at the table size) until it reaches the key or a
 * truly-empty cell. Table sizes are primes from hashtable_sizes.h and the load
 * factor is capped at 1/3 (check_load() rehashes once used_count_ > size/3).
 *
 * Stride 1 is deliberate. An earlier version used a triangular-number quadratic
 * probe (hashval += ++probe), which on a prime-sized table only visits ~half the
 * slots: lookups could miss an existing key — or fail to find a free cell — while
 * the table was far from full, and the count>size fallback could then re-enter
 * the rehash path and corrupt the table. Linear probing visits every slot for
 * any modulus, so the walk always terminates at the key or an empty cell.
 *
 * Deletion uses tombstones: remove() clears the used_ bit and sets the clear_
 * bit but leaves the slot in the probe chain. A tombstone is NOT end-of-chain (a
 * key inserted before the deletion may live further along), so find_pair() only
 * stops scanning on a cell that is neither used nor a tombstone. Key equality is
 * never tested against an unused slot — a tombstone's Pair has been destructed,
 * and for trivial keys the stale bits could spuriously match. Tombstones are
 * reclaimed on the next rehash (clear_clearcells() rehashes to the same size).
 *
 * Slot state is two bitmaps: used_ (slot holds a live entry) and clear_ (slot is
 * a tombstone). used_count_ is the live-entry count that drives rehashing; it is
 * maintained across rehashes but is not decremented by remove(), so size() can
 * overcount tombstones until the next rehash.
 */

#include "alloc.h"
#include "boolvector.h"
#include "compiler_util.h"
#include "concepts.h"
#include "hash.h"
#include "hashtable_sizes.h"

#include <concepts>
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
} // namespace detail::map

namespace detail::map {
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
 * Open-addressing hash map with linear probing (stride 1; see the file header
 * comment for the probing strategy and tombstone semantics).
 *
 * Stores up to @p static_size key-value pairs inline (actual table capacity
 * is roughly 3x to maintain load factor). Falls back to heap via alloc::alloc
 * when the element count exceeds the static capacity. Rehashes when more than
 * one-third of slots are occupied. Uses tombstones for deletion.
 */
template <typename Key, typename Value, int static_size = 16>
class alignas(ContainerAlign<detail::map::Pair<Key, Value>>()) Map {
  using Pair = detail::map::Pair<Key, Value>;
  const static int real_static_size = static_size * 3 + 1;

public:
  using key_type = Key;
  using value_type = Value;

  struct iterator {
    iterator(const Map *map, int i) : map_(map), i_(i)
    {
      if (i_ == 0) {
        /* Find first item. */
        i_ = -1;
        operator++();
      }
    }

    iterator(const iterator &b) : map_(b.map_), i_(b.i_)
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

      while (i_ < map_->table_.size() && !map_->used_[i_]) {
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

      int table_size = int(map_->table_.size());

      while (i_ < map_->table_.size() && !map_->used_[i_]) {
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
      return key_value_range(map_, map_->table_.size());
    }

  private:
    const Map *map_;
    int i_ = 0;
  };

  using key_range = key_value_range<true, Key>;
  using value_range = key_value_range<false, Value>;

  friend struct key_value_range<true, Key>;

  Map(const Map &b) : cur_size_(b.cur_size_)
  {
    int size = hashsizes[cur_size_];

    if (size <= real_static_size) {
      table_ = std::span(get_static(), size);
    } else {
      table_ = std::span(
          static_cast<Pair *>(alloc::alloc("copied map table", sizeof(Pair) * size)),
          size);
    }

    used_ = b.used_;
    used_count_ = b.used_count_;
    clear_ = b.clear_;

    for (int i = 0; i < size; i++) {
      if (used_[i]) {
        /* table_ is raw storage; copy-construct rather than assign so a
         * non-trivial Key/Value isn't assigned over uninitialized memory. */
        new (static_cast<void *>(&table_[i])) Pair(b.table_[i]);
      }
    }
  }

  Map()
      : table_(get_static(), hashsizes[find_hashsize_prev(real_static_size)]),
        cur_size_(find_hashsize_prev(real_static_size))
  {
    reserve_usedmap();
  }

  Map(Map &&b)
      : used_(std::move(b.used_)), clear_(std::move(b.clear_)),
        cur_size_(b.cur_size_), used_count_(b.used_count_)
  {
    if (b.table_.data() == b.get_static()) {
      /* b's table lives inline; we can't steal the pointer (it points into b),
       * so move the live pairs into our own static storage. */
      table_ = std::span(get_static(), b.table_.size());

      if constexpr (!Pair::is_simple()) {
        for (int i = 0; i < b.table_.size(); i++) {
          if (used_[i]) {
            new (static_cast<void *>(&table_[i])) Pair(std::move(b.table_[i]));
            b.table_[i].~Pair();
          }
        }
      } else {
        memcpy(static_cast<void *>(get_static()),
               static_cast<const void *>(b.table_.data()),
               sizeof(Pair) * b.table_.size());
      }
    } else {
      /* b's table is heap-allocated; steal the buffer outright. */
      table_ = b.table_;
    }

    /* Reset b to an empty inline state so its destructor neither frees the
     * buffer we now own nor touches the pairs we moved out. */
    b.table_ = std::span(b.get_static(), size_t(0));
    b.used_count_ = 0;
  }

  DEFAULT_COPY_ASSIGNMENT(Map)
  DEFAULT_MOVE_ASSIGNMENT(Map)

  Map(std::initializer_list<Pair> list)
  {
    cur_size_ = find_hashsize(list.size() * 3 + 1);
    int size = hashsizes[cur_size_];

    if (size <= real_static_size) {
      table_ = std::span(get_static(), size);
    } else {
      table_ = std::span(
          static_cast<Pair *>(alloc::alloc("Map table", sizeof(Pair) * size)), size);
    }

    reserve_usedmap();

    for (auto &&item : list) {
      add_overwrite(item.key, item.value);
    }
  }

  ~Map()
  {
    if (table_.data() == get_static()) {
      return;
    }

    if constexpr (!Pair::is_simple()) {
      for (int i = 0; i < table_.size(); i++) {
        if (used_[i]) {
          table_[i].~Pair();
        }
      }
    }

    alloc::release(static_cast<void *>(table_.data()));
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
    return iterator(this, table_.size());
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
    check_load();

    int i = find_pair<false, true>(key);
    add_finalize(i, key, value);
  }

  /** Rvalue overload of insert method above.  Does not check if key already exists.*/
  void insert(Key &&key, Value &&value)
  {
    check_load();

    int i = find_pair<false, true>(key);
    add_finalize(i, key, value);
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
    check_load();
    int first_clearcell = -1;
    int i = find_pair<true, true>(key, &first_clearcell);

    if (!used_[i]) {
      if (first_clearcell != -1) {
        i = first_clearcell;
        clear_.set(first_clearcell, false);
      }

      /* Simple values must be explicitly zeroed — the slot is raw storage. */
      if constexpr (!is_simple<Value>()) {
        new (static_cast<void *>(&table_[i].value)) Value();
      } else {
        table_[i].value = Value();
      }

      used_.set(i, true);
      new (static_cast<void *>(&table_[i].key)) Key(key);
      used_count_++;
    }
    return table_[i].value;
  }

  /** Returns true if @p key is present in the map. */
  bool contains(const Key &key) const
  {
    int i = const_cast<Map *>(this)->find_pair<true, false>(key);
    return i != -1;
  }
  bool contains(const Key &&key) const
  {
    int i = const_cast<Map *>(this)->find_pair<true, false>(key);
    return i != -1;
  }

  /** Returns a pointer to the value for @p key, or nullptr if not found. */
  Value *lookup_ptr(const Key &key)
  {
    int i = find_pair<true, false>(key);
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
    check_load();

    int first_clearcell = -1;
    int i = find_pair<true, true>(key, &first_clearcell);
    if (!used_[i]) {
      if (first_clearcell != -1) {
        i = first_clearcell;
        clear_.set(first_clearcell, false);
      }

      used_.set(i, true);
      used_count_++;

      /* Use copy/move constructors since we have unallocated memory. */
      new (static_cast<void *>(&table_[i].key)) Key(copy_key(key));
      new (static_cast<void *>(&table_[i].value)) Value(set_value());
    }

    return table_[i].value;
  }

  /**
   * Inserts @p key if absent and writes a pointer to the value slot into
   * @p value. The value slot is default-initialized for non-trivial types
   * so the caller can use assignment rather than placement new. Returns true
   * if the key was newly inserted.
   */
  bool add_uninitialized(const Key &key, Value **value)
  {
    check_load();

    int first_clearcell = -1;
    int i = find_pair<true, true>(key, &first_clearcell);

    if (value) {
      *value = &table_[i].value;
    }

    if (!used_[i]) {
      if (first_clearcell != -1) {
        i = first_clearcell;
        clear_.set(first_clearcell, false);
      }
      // make life easier to client code by
      // default initializing the value, which allows them to
      // use assignment operator instead of placement new.
      if (!is_simple<Value>()) {
        new (static_cast<void *>(&table_[i].value)) Value();
      }

      // use placement new instead of assignment
      new (static_cast<void *>(&table_[i].key)) Key(key);
      used_.set(i, true);
      used_count_++;
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
    int i = find_pair<true, false>(key);
    return table_[i].value;
  }
  
  const Value &lookup(const Key &key) const
  {
    int i = const_cast<Map *>(this)->find_pair<true, false>(key);
    return table_[i].value;
  }

  /**
   * Removes @p key from the map. If @p out_value is non-null, the removed
   * value is moved into it. Returns true if the key was found and removed.
   */
  bool remove(const Key &key, Value *out_value = nullptr)
  {
    int i = find_pair<true, false>(key);

    if (i == -1) {
      return false;
    }

    this->used_.set(i, false);
    this->clear_.set(i, true);

    if (out_value) {
      *out_value = std::move(table_[i].value);
    }

    if constexpr (!Pair::is_simple()) {
      table_[i].~Pair();
    }

    return true;
  }

  /**
   * Pre-allocates table capacity for at least @p size entries without
   * changing the current contents.
   */
  void reserve(size_t size)
  {
    size = size * 3 + 1;

    if (table_.size() >= size) {
      return;
    }

    realloc_to_size(size);
  }

private:
  using MyBoolVector = BoolVector<static_size * 3 + 1>;

  std::span<Pair> table_;
  char static_storage_[real_static_size * sizeof(Pair)];
  MyBoolVector used_;
  // used to mark deleted tombstones
  MyBoolVector clear_;
  int cur_size_ = 0;
  int used_count_ = 0;

  void reserve_usedmap()
  {
    hash::HashInt size = hash::HashInt(hashsizes[cur_size_]);
    used_.resize(size);
    used_.clear();
    clear_.resize(size);
    clear_.clear();
  }

  template <bool overwrite = false> bool add_intern(const Key &key, const Value &value)
  {
    check_load();

    int first_clearcell = -1;
    int i = find_pair<true, true>(key, &first_clearcell);
    if (used_[i]) {
      if constexpr (overwrite) {
        add_finalize(i, key, value);
      }

      return false;
    }

    /* Reuse a tombstone in the probe chain if one was found, instead of
     * consuming a fresh empty cell (which would rehash sooner). add_finalize
     * clears the tombstone bit. */
    if (first_clearcell != -1) {
      i = first_clearcell;
    }

    add_finalize(i, key, value);

    return true;
  }

  /* Handles rvalues. */
  template <typename KeyArg, typename ValueArg>
  void add_finalize(int i, const KeyArg key, const ValueArg value)
  {
    const bool was_used = used_[i];

    if constexpr (!Pair::is_simple()) {
      if (!was_used) {
        /* Use copy/move constructors. */
        new (static_cast<void *>(&table_[i].key)) Key(key);
        new (static_cast<void *>(&table_[i].value)) Value(value);
      } else {
        table_[i].key = key;
        table_[i].value = value;
      }
    } else {
      table_[i].key = key;
      table_[i].value = value;
    }

    /* Only bump the live count when we actually occupied a new slot; an
     * overwrite of an existing key must not inflate used_count_ (which would
     * trigger spurious rehashes — the riskiest map operation). */
    if (!was_used) {
      used_count_++;
    }
    used_.set(i, true);
    clear_.set(i, false);
  }

  template <bool check_key_equals = true, bool return_unused_cell = false>
  int find_pair(const Key &key, int *first_clearcell = nullptr)
  {
    const hash::HashInt size = hash::HashInt(table_.size());
    const hash::HashInt hashval = hash::hash(key) % size;
    hash::HashInt h = hashval;

    /* Linear probing (stride 1). The table sizes are primes; the previous
     * triangular-number quadratic probe (h += ++probe) only visits ~half the
     * slots of a prime-sized table, so it could fail to find an empty slot — or
     * an existing key — even when the table is far from full, and the resulting
     * clear_clearcells() retry could re-enter realloc_to_size mid-rehash and
     * corrupt the table. Stride 1 visits every slot for any modulus, so the
     * probe always terminates at the key or a truly-empty cell. */
    int count = 0;
    while (1) {
      if (count++ > int(size)) {
        clear_clearcells();
        return find_pair<check_key_equals, return_unused_cell>(key, first_clearcell);
      }

      if (!used_[h]) {
        if constexpr (return_unused_cell) {
          if (!clear_[h]) {
            return h;
          } else if (first_clearcell && *first_clearcell == -1) {
            /* Record the first tombstone so the caller can reuse it. */
            *first_clearcell = h;
          }
        } else {
          /* A tombstone (used_=false, clear_=true) is a deleted slot, not the
           * end of the probe chain — a key inserted before the deletion may
           * still live further along. Only a truly empty cell (not used, not a
           * tombstone) means the key is absent. Returning -1 on a tombstone made
           * lookup/contains/remove miss keys whose chain crossed a deleted slot. */
          if (!clear_[h]) {
            return -1;
          }
        }
        /* Never run the key-equality check on an unused slot. A tombstone's Pair
         * has been destructed; for trivially-destructible keys (pointers, ints)
         * the stale key bits linger, so comparing them can spuriously match a
         * just-removed key whose live, re-inserted entry sits further down the
         * probe chain — producing a duplicate live entry. */
      } else if constexpr (check_key_equals) {
        if (table_[h].key == key) {
          return h;
        }
      }

      h++;
      if (h >= size) {
        h -= size;
      }
    }
  }

  inline bool check_load()
  {
    if (used_count_ > table_.size() / 3) {
      realloc_to_size(table_.size() * 3);
      return true;
    }

    return false;
  }

  inline Map &clear()
  {
    // destruct all used pairs
    if constexpr (!Pair::is_simple()) {
      int size = table_.size();
      for (int i = 0; i < size; i++) {
        if (!(used_[i])) {
          continue;
        }

        table_[i].~Pair();
      }
    }

    // reset used map and count
    used_count_ = 0;
    used_.clear();
    clear_.clear();
    return *this;
  }

  void clear_clearcells()
  {
    realloc_to_size(table_.size());
  }

  inline void realloc_to_size(size_t size)
  {
    size_t old_size = hashsizes[cur_size_];
    while (hashsizes[cur_size_] < size) {
      cur_size_++;
    }

    size_t newsize = hashsizes[cur_size_];

    std::span<Pair> old = table_;
    MyBoolVector old_used = used_;

    table_ = std::span(static_cast<Pair *>(alloc::alloc("sculpecore::util::map table",
                                                        newsize * sizeof(Pair))),
                       newsize);

    used_count_ = 0;
    used_.resize(newsize);
    used_.clear();
    clear_.resize(newsize);
    clear_.clear();

    for (int i = 0; i < old.size(); i++) {
      if (old_used[i]) {
        int index = find_pair<false, true>(old[i].key);

        new (static_cast<void *>(&table_[index].key)) Key(std::move(old[i].key));
        new (static_cast<void *>(&table_[index].value)) Value(std::move(old[i].value));

        if constexpr (!Pair::is_simple()) {
          old[i].~Pair();
        }
        used_.set(index, true);
        /* Maintain the live-entry count across the rehash. Leaving it at 0
         * (the reset above) lets check_load() think the table is empty, so it
         * never rehashes again until ~size/3 *new* inserts accumulate — by then
         * the table runs at a pathological load factor, with every probe walking
         * a near-full chain. */
        used_count_++;
      }
    }

    if (old.data() != get_static()) {
      alloc::release(static_cast<void *>(old.data()));
    }
  }

  Pair *get_static()
  {
    return reinterpret_cast<Pair *>(static_storage_);
  }
};
} // namespace litestl::util
