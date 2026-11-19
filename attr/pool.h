#pragma once

/* InternPool<T>: immutable, content-deduplicated pool entries, one pool per
 * kind inside a PoolStore (attr/pool_store.h). See documentation/attributes.md
 * ("Value pools") for the owner pattern.
 *
 * - Every add interns: two live slots of a pool never hold equal values.
 * - A slot is live iff it holds a node; only PoolStore::collect/canonicalize
 *   free slots. Between collects, unreachable entries stay live and intern may
 *   hand them out again (harmless: entries are immutable).
 * - Copying a pool shares its slot pages and index (O(pages)); snapshots
 *   diverge independently. Nodes are shared between snapshots and freed when
 *   no slot page references them.
 * - Mutation (intern, free, renumber, read) goes through PoolStore, one
 *   writer per store. */

#include <atomic>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "attr/paged_store.h"
#include "attr/pool_ref.h"
#include "attr/pool_slots.h"
#include "io/bytes.h"
#include "util/function.h"
#include "util/hash128.h"
#include "util/memory.h"
#include "util/string.h"
#include "util/vector.h"

namespace litestl::attr {

struct PoolReadLimits {
  uint32_t max_slots = 1u << 26;            /* per pool */
  uint64_t max_entry_bytes = 1ull << 30;    /* serialized bytes of one entry */
  bool verify_hash = true;                  /* test-only: false lets fuzzers past the trailing hash */
};

/* ------------------------------------------------------------------------
 * PoolTraits
 * ---------------------------------------------------------------------- */

namespace detail {
template <typename T>
concept RawArith = std::is_arithmetic_v<T> && !std::is_same_v<T, bool>;

template <typename T> struct RawPoolShape : std::bool_constant<RawArith<T>> {};
template <typename E, int N> struct RawPoolShape<math::Vec<E, N>> : std::bool_constant<RawArith<E>> {};
template <typename E, size_t N> struct RawPoolShape<std::array<E, N>> : std::bool_constant<RawArith<E>> {};

/* probes for the has_refs member requirements */
struct RefProbe {
  void operator()(PoolRef) const {}
};
struct RefMapProbe {
  PoolRef operator()(PoolRef r) const
  {
    return r;
  }
};
} // namespace detail

/** Member-function protocol for pool value types (P1b §3.4). equal() must be
 * bitwise for floating-point members, consistent with hash_into(). */
template <typename T>
concept MemberPoolValue = requires(const T &a, T &m, hash::Hasher128 &h, io::ByteWriter &w, io::ByteReader &r,
                                   const PoolReadLimits &lim) {
  { a.hash_into(h) };
  { a.equal(a) } -> std::same_as<bool>;
  { a.write(w) };
  { T::read(r, m, lim) } -> std::same_as<bool>;
  { T::has_refs } -> std::convertible_to<bool>;
};

template <typename T> struct PoolTraits; /* undefined: not a pool value type */

/** Arithmetic scalars, Vec and std::array of them: raw little-endian bytes. */
template <typename T>
  requires(detail::RawPoolShape<T>::value)
struct PoolTraits<T> {
  static constexpr bool has_refs = false;
  static constexpr uint32_t version = 1;
  static void hash(const T &v, hash::Hasher128 &h)
  {
    h.raw_bytes(&v, sizeof(T));
  }
  static bool equal(const T &a, const T &b)
  {
    return memcmp(static_cast<const void *>(&a), static_cast<const void *>(&b), sizeof(T)) == 0;
  }
  static void write(const T &v, io::ByteWriter &w)
  {
    w.put(&v, sizeof(T));
  }
  static bool read(io::ByteReader &r, T &v, const PoolReadLimits &)
  {
    return r.get(&v, sizeof(T));
  }
  template <typename F> static void for_each_ref(const T &, F &&) {}
  template <typename F> static void remap_refs(T &, F &&) {}
};

template <> struct PoolTraits<util::string> {
  static constexpr bool has_refs = false;
  static constexpr uint32_t version = 1;
  static void hash(const util::string &v, hash::Hasher128 &h)
  {
    h.str(std::string_view(v));
  }
  static bool equal(const util::string &a, const util::string &b)
  {
    return std::string_view(a) == std::string_view(b);
  }
  static void write(const util::string &v, io::ByteWriter &w)
  {
    w.str(std::string_view(v));
  }
  static bool read(io::ByteReader &r, util::string &v, const PoolReadLimits &)
  {
    const std::string_view s = r.str();
    if (r.failed() || s.find('\0') != std::string_view::npos) {
      return false;
    }
    v = util::string(std::string(s));
    return true;
  }
  template <typename F> static void for_each_ref(const util::string &, F &&) {}
  template <typename F> static void remap_refs(util::string &, F &&) {}
};

template <typename T>
  requires(MemberPoolValue<T> && !detail::RawPoolShape<T>::value)
struct PoolTraits<T> {
  static constexpr bool has_refs = bool(T::has_refs);
  static constexpr uint32_t version = [] {
    if constexpr (requires { T::pool_version; }) {
      return uint32_t(T::pool_version);
    }
    else {
      return uint32_t(1);
    }
  }();
  static_assert(!has_refs || requires(const T &a, T &m) {
    a.for_each_ref(detail::RefProbe{});
    m.remap_refs(detail::RefMapProbe{});
  }, "a pool value type with has_refs = true needs for_each_ref(f) and remap_refs(f)");

  static void hash(const T &v, hash::Hasher128 &h)
  {
    v.hash_into(h);
  }
  static bool equal(const T &a, const T &b)
  {
    return a.equal(b);
  }
  static void write(const T &v, io::ByteWriter &w)
  {
    v.write(w);
  }
  static bool read(io::ByteReader &r, T &v, const PoolReadLimits &lim)
  {
    return T::read(r, v, lim);
  }
  template <typename F> static void for_each_ref(const T &v, F &&f)
  {
    if constexpr (has_refs) {
      v.for_each_ref(f);
    }
  }
  template <typename F> static void remap_refs(T &v, F &&f)
  {
    if constexpr (has_refs) {
      v.remap_refs(f);
    }
  }
};

template <typename T>
concept PoolValue = requires { PoolTraits<T>::has_refs; } && std::is_move_constructible_v<T> &&
                    std::is_default_constructible_v<T>;

/* ------------------------------------------------------------------------
 * PoolBase: the type-erased interface PoolStore drives
 * ---------------------------------------------------------------------- */

class PoolStore;

class PoolBase {
public:
  virtual ~PoolBase() = default;

  std::string_view key() const
  {
    return std::string_view(key_.c_str());
  }
  virtual const void *class_tag() const = 0;
  virtual uint32_t traits_version() const = 0;
  virtual bool has_refs() const = 0;

  virtual uint32_t slot_count() const = 0;
  virtual uint32_t live_count() const = 0;
  virtual bool live(uint32_t slot) const = 0;
  virtual hash::Hash128 entry_hash(uint32_t slot) const = 0;
  /** entry_hash of the entry with its inner refs rewritten through m. */
  virtual hash::Hash128 entry_hash_mapped(uint32_t slot, const RefRemap &m) const = 0;
  virtual void for_each_inner_ref(uint32_t slot, util::function_ref<void(PoolRef)> f) const = 0;

  /** Snapshot (shares pages, index and nodes). */
  virtual util::UniquePtr<PoolBase> clone() const = 0;
  /** Empty pool of the same class and key. */
  virtual util::UniquePtr<PoolBase> new_empty() const = 0;

  /* ---- store-driven mutation ---- */
  /** Frees a live slot (index removal; free list or quarantine). */
  virtual void free_slot(uint32_t slot, bool quarantine) = 0;
  /** Rebuilds compact: new slot n holds old slot new_to_old[n]; inner refs are
   * remapped through m (nodes whose refs change are replaced). */
  virtual void renumber(util::span<const uint32_t> new_to_old, const RefRemap &m) = 0;
  /** Interns a copy of src's entry with inner refs mapped through m (import).
   * Returns the slot, and *miss when a new slot was taken. */
  virtual uint32_t intern_copy(const PoolBase &src, uint32_t src_slot, const RefRemap &m, bool *miss) = 0;

  /* ---- I/O and audit ---- */
  virtual void write_entries(io::ByteWriter &w, uint32_t k) const = 0;
  /** Appends n entries to an empty pool; false on malformed input, limits or
   * duplicate values. */
  virtual bool read_entries(io::ByteReader &r, uint32_t n, const PoolReadLimits &lim, const char **err) = 0;
  /** Free list, index and node checks; appends problems. */
  virtual void audit_internal(util::Vector<util::string> &problems) const = 0;

  uint64_t taken_since_collect() const
  {
    return taken_;
  }
  void reset_taken()
  {
    taken_ = 0;
  }
  /** Bytes copied by copy-on-write of slot pages and index (benchmarks). */
  virtual uint64_t cow_bytes() const = 0;

protected:
  util::string key_;
  uint64_t taken_ = 0;
  friend class PoolStore;
};

/* ------------------------------------------------------------------------
 * InternPool<T>
 * ---------------------------------------------------------------------- */

constexpr int kDefaultSlotShift = 10;

template <PoolValue T, int SlotShift = kDefaultSlotShift> class InternPool final : public PoolBase {
public:
  using value_type = T;
  using Traits = PoolTraits<T>;

  struct Node {
    std::atomic<uint32_t> holders{1};
    hash::Hash128 hash;
    T value;
    void *block = nullptr;

    static void retain(Node *n)
    {
      n->holders.fetch_add(1, std::memory_order_relaxed);
    }
    static void release(Node *n)
    {
      if (n->holders.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        void *b = n->block;
        n->~Node();
        alloc::release(b);
      }
    }
  };
  static_assert(alignof(Node) >= 4, "slot words use the low two bits of node pointers");

  InternPool() = default;
  explicit InternPool(std::string_view key)
  {
    key_ = util::string(std::string(key));
  }

  static const void *static_tag()
  {
    static const char tag = 0;
    return &tag;
  }
  const void *class_tag() const override
  {
    return static_tag();
  }
  uint32_t traits_version() const override
  {
    return Traits::version;
  }
  bool has_refs() const override
  {
    return Traits::has_refs;
  }

  uint32_t slot_count() const override
  {
    return slots_.size();
  }
  uint32_t live_count() const override
  {
    return live_;
  }
  bool live(uint32_t s) const override
  {
    return s < slots_.size() && detail::word_is_node(slots_.get(s));
  }
  hash::Hash128 entry_hash(uint32_t s) const override
  {
    return node(s)->hash;
  }
  hash::Hash128 entry_hash_mapped(uint32_t s, const RefRemap &m) const override
  {
    if constexpr (Traits::has_refs) {
      T v = node(s)->value;
      bool changed = false;
      Traits::remap_refs(v, [&](PoolRef r) {
        const PoolRef mr = m.map(r);
        changed |= !(mr == r);
        return mr;
      });
      if (changed) {
        return hash_of(v);
      }
    }
    return node(s)->hash;
  }
  void for_each_inner_ref(uint32_t s, util::function_ref<void(PoolRef)> f) const override
  {
    if constexpr (Traits::has_refs) {
      Traits::for_each_ref(node(s)->value, [&](PoolRef r) { f(r); });
    }
  }

  /** The entry of a live slot. Valid until the owning store's next collect,
   * canonicalize, read, assignment, move-from or destruction. */
  const T &get(uint32_t s) const
  {
    return node(s)->value;
  }

  static hash::Hash128 hash_of(const T &v)
  {
    hash::Hasher128 h(hash::DomainTag::PoolEntry);
    Traits::hash(v, h);
    return h.finish();
  }

  /** Live slot holding a value equal to v, or -1. */
  int64_t find(const T &v) const
  {
    return find_hashed(v, hash_of(v));
  }

  util::UniquePtr<PoolBase> clone() const override
  {
    auto p = util::make_unique<InternPool>("attr::InternPool", *this);
    return util::UniquePtr<PoolBase>(std::move(p));
  }
  util::UniquePtr<PoolBase> new_empty() const override
  {
    auto p = util::make_unique<InternPool>("attr::InternPool", key());
    return util::UniquePtr<PoolBase>(std::move(p));
  }

  InternPool(const InternPool &b) = default;
  InternPool &operator=(const InternPool &b) = default;

  void free_slot(uint32_t s, bool quarantine) override
  {
    Node *n = node(s);
    index_.erase(uint32_t(n->hash.lo), s);
    if (quarantine) {
      slots_.set(s, detail::kQuarantined);
    }
    else {
      slots_.set(s, detail::free_word(free_head_));
      free_head_ = s;
    }
    live_--;
  }

  void renumber(util::span<const uint32_t> new_to_old, const RefRemap &m) override
  {
    detail::SlotTable<Node, SlotShift> out;
    for (uint32_t o : new_to_old) {
      Node *n = node(o);
      if constexpr (Traits::has_refs) {
        T v = n->value;
        bool changed = false;
        Traits::remap_refs(v, [&](PoolRef r) {
          const PoolRef mr = m.map(r);
          changed |= !(mr == r);
          return mr;
        });
        if (changed) {
          const hash::Hash128 h = hash_of(v);
          out.push_back(reinterpret_cast<detail::SlotWord>(make_node(std::move(v), h)));
          continue;
        }
      }
      Node::retain(n);
      out.push_back(reinterpret_cast<detail::SlotWord>(n));
    }
    slots_ = std::move(out);
    live_ = uint32_t(new_to_old.size());
    free_head_ = detail::kFreeEnd;
    rebuild_index();
  }

  uint32_t intern_copy(const PoolBase &src, uint32_t src_slot, const RefRemap &m, bool *miss) override
  {
    const InternPool &s = static_cast<const InternPool &>(src);
    T v = s.get(src_slot);
    Traits::remap_refs(v, [&](PoolRef r) { return m.map(r); });
    return intern(std::move(v), miss);
  }

  void write_entries(io::ByteWriter &w, uint32_t k) const override
  {
    for (uint32_t s = 0; s < k; s++) {
      Traits::write(get(s), w);
    }
  }

  bool read_entries(io::ByteReader &r, uint32_t n, const PoolReadLimits &lim, const char **err) override
  {
    auto fail = [&](const char *m) {
      if (err) {
        *err = m;
      }
      return false;
    };
    for (uint32_t i = 0; i < n; i++) {
      const size_t before = r.remaining();
      T v{};
      if (!Traits::read(r, v, lim) || r.failed()) {
        return fail("pool read: malformed entry");
      }
      if (before - r.remaining() > lim.max_entry_bytes) {
        return fail("pool read: entry exceeds max_entry_bytes");
      }
      bool miss = false;
      intern(std::move(v), &miss);
      if (!miss) {
        return fail("pool read: duplicate entry");
      }
    }
    return true;
  }

  void audit_internal(util::Vector<util::string> &problems) const override
  {
    auto problem = [&](const std::string &m) {
      problems.append(util::string((std::string(key()) + ": " + m).c_str()));
    };
    /* free list: exactly the free slots, no loop */
    uint32_t nfree = 0, nlive = 0;
    for (uint32_t s = 0; s < slots_.size(); s++) {
      const detail::SlotWord w = slots_.get(s);
      if (detail::word_is_node(w)) {
        nlive++;
        const Node *n = reinterpret_cast<const Node *>(w);
        if (n->holders.load(std::memory_order_relaxed) == 0) {
          problem("node with zero holders at slot " + std::to_string(s));
        }
        if (!(hash_of(n->value) == n->hash)) {
          problem("stale node hash at slot " + std::to_string(s));
        }
        if (find_hashed(n->value, n->hash) != int64_t(s)) {
          problem("index does not resolve slot " + std::to_string(s) + " (missing or duplicate value)");
        }
      }
      else if (detail::word_is_free(w)) {
        nfree++;
      }
      else if (w != detail::kQuarantined) {
        problem("bad slot word at " + std::to_string(s));
      }
    }
    if (nlive != live_) {
      problem("live count mismatch");
    }
    uint32_t walked = 0;
    for (uint32_t s = free_head_; s != detail::kFreeEnd; walked++) {
      if (s >= slots_.size() || !detail::word_is_free(slots_.get(s)) || walked > nfree) {
        problem("free list broken or looping");
        return;
      }
      s = detail::free_next(slots_.get(s));
    }
    if (walked != nfree) {
      problem("free list does not hold every free slot");
    }
  }

  uint64_t cow_bytes() const override
  {
    return index_.copied_bytes();
  }
  /** Slot pages this pool shares with o (benchmarks and tests). */
  uint32_t shared_slot_pages_with(const InternPool &o) const
  {
    uint32_t n = 0;
    for (uint32_t p = 0; p < slots_.page_count(); p++) {
      n += slots_.shares_page_with(o.slots_, p) ? 1 : 0;
    }
    return n;
  }
  uint32_t slot_page_count() const
  {
    return slots_.page_count();
  }
  static constexpr size_t slot_page_bytes()
  {
    return detail::SlotTable<Node, SlotShift>::page_bytes();
  }

  /** Interns v; returns its slot and sets *miss when a new slot was taken.
   * PoolStore::intern is the public entry point (it validates inner refs). */
  uint32_t intern(T &&v, bool *miss)
  {
    const hash::Hash128 h = hash_of(v);
    const int64_t hit = find_hashed(v, h);
    if (hit >= 0) {
      *miss = false;
      return uint32_t(hit);
    }
    *miss = true;
    if (index_.needs_rebuild(live_ + 1)) {
      rebuild_index();
    }
    Node *n = make_node(std::move(v), h);
    uint32_t s;
    if (free_head_ != detail::kFreeEnd) {
      s = free_head_;
      free_head_ = detail::free_next(slots_.get(s));
      slots_.set(s, reinterpret_cast<detail::SlotWord>(n));
    }
    else {
      s = slots_.size();
      if (s >= detail::kFreeEnd) {
        fprintf(stderr, "litestl::attr: pool %s is full\n", key_.c_str());
        abort();
      }
      slots_.push_back(reinterpret_cast<detail::SlotWord>(n));
    }
    index_.insert(uint32_t(h.lo), s);
    live_++;
    taken_++;
    return s;
  }

private:
  detail::SlotTable<Node, SlotShift> slots_;
  detail::InternIndex index_;
  uint32_t live_ = 0;
  uint32_t free_head_ = detail::kFreeEnd;

  const Node *node(uint32_t s) const
  {
    const detail::SlotWord w = slots_.get(s);
    if (!detail::word_is_node(w)) {
      fprintf(stderr, "litestl::attr: pool %s slot %u is not live\n", key_.c_str(), s);
      abort();
    }
    return reinterpret_cast<const Node *>(w);
  }
  Node *node(uint32_t s)
  {
    return const_cast<Node *>(static_cast<const InternPool *>(this)->node(s));
  }

  static Node *make_node(T &&v, const hash::Hash128 &h)
  {
    void *block = alloc::alloc("attr::PoolNode", sizeof(Node) + alignof(Node) + 16);
    const size_t align = alignof(Node) > 16 ? alignof(Node) : 16;
    const uintptr_t base = (reinterpret_cast<uintptr_t>(block) + align - 1) & ~uintptr_t(align - 1);
    Node *n = new (reinterpret_cast<void *>(base)) Node{{1}, h, std::move(v), block};
    return n;
  }

  int64_t find_hashed(const T &v, const hash::Hash128 &h) const
  {
    return index_.find(uint32_t(h.lo), [&](uint32_t s) {
      if (s >= slots_.size()) {
        return false;
      }
      const detail::SlotWord w = slots_.get(s);
      if (!detail::word_is_node(w)) {
        return false;
      }
      const Node *n = reinterpret_cast<const Node *>(w);
      return n->hash == h && Traits::equal(n->value, v);
    });
  }

  void rebuild_index()
  {
    index_.rebuild(live_, [&](auto &&add) {
      for (uint32_t s = 0; s < slots_.size(); s++) {
        const detail::SlotWord w = slots_.get(s);
        if (detail::word_is_node(w)) {
          add(uint32_t(reinterpret_cast<const Node *>(w)->hash.lo), s);
        }
      }
    });
  }
};

} // namespace litestl::attr
