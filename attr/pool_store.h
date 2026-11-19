#pragma once

/* PoolStore: owns the value pools of one owner snapshot (e.g. a complex).
 *
 * Value semantics: copying a store is an O(pages) snapshot; snapshots diverge
 * independently. Nothing points into a store, so owners keep defaulted copy
 * and move.
 *
 * Liveness is mark-and-sweep (P1b, DEC-015). PoolRef values in columns don't
 * keep entries alive; collect(roots) frees every entry not reachable from
 *   - the ref columns of the root groups (Temp/Derived included),
 *   - the extra roots (RefRoot: a journal, tests),
 *   - refs inside reachable entries.
 * Rooting rule: a PoolRef held anywhere else (a local between intern and the
 * column assignment) must not be held across collect/canonicalize. Owners
 * collect at deterministic points (operation boundaries, thresholds on
 * taken_since_collect(), before canonicalize/write).
 *
 * Canonical form: canonicalize(roots) renumbers slots by first reference in
 * three phases (hashed columns, then Temp/Derived, then extra roots, each with
 * its own inner-ref closure) and compacts. Bytes and hashes cover the hashed
 * closure only, and require canonical(roots). Owner write: copy the owner,
 * canonicalize the copy, write the store then the groups. Owner read: fresh
 * owner, store.read, group reads, audit(require_reachable), canonicalize must
 * report no change, then move-assign.
 *
 * Refs copied raw between stores are meaningless; use import/import_rows. */

#include "attr/group.h"
#include "attr/pool.h"
#include "util/map.h"

namespace litestl::attr {

/** External holder of refs (P3 journal, weak caches). */
class RefRoot {
public:
  virtual ~RefRoot() = default;
  /** Strong refs: kept alive by collect. */
  virtual void visit_refs(RefVisitor &v) const = 0;
  virtual void remap_refs(const RefRemap &m) = 0;
  /** Called after mark, before sweep: drop weak refs to unmarked entries. */
  virtual void prune(const MarkQuery &) {}
};

class PoolStore {
public:
  using AcceptFn = util::function_ref<bool(const RefSite &, PoolRef)>;

  struct Roots { /* in the owner's canonical order */
    util::span<const AttrGroup *const> groups;
    util::span<RefRoot *const> extra;
    const AcceptFn *accept = nullptr; /* optional kind check (B7) */
  };
  struct MutRoots {
    util::span<AttrGroup *const> groups;
    util::span<RefRoot *const> extra;
    const AcceptFn *accept = nullptr;
  };

  /** The phase-1 (hashed) part of canonicalize's numbering, computed without
   * changing anything: maps a ref of the closure to the ref it would get.
   * Refs outside the closure (only Temp/Derived or extra roots hold them) map
   * to an unset slot. Valid until the store or a root group changes. */
  class CanonicalMap final : public RefRemap {
  public:
    PoolRef map(PoolRef r) const override;
    /** Entries of the closure per pool, in canonical order (old slots). */
    util::Vector<util::Vector<uint32_t>> new_to_old;
    util::Vector<util::Vector<uint32_t>> old_to_new;
  };

  struct ImportMemo {
    util::Map<uint64_t, PoolRef> map; /* (src pool << 32 | slot) -> dst ref */
  };

  PoolStore();
  PoolStore(const PoolStore &b);
  PoolStore &operator=(const PoolStore &b);
  PoolStore(PoolStore &&b) noexcept = default;
  PoolStore &operator=(PoolStore &&b) noexcept = default;
  ~PoolStore() = default;

  /* ---- layout ---- */
  /** Registers a pool; ids are 1, 2, ... in call order (the owner's fixed
   * layout; append new kinds at the end). Aborts on a duplicate key. */
  template <typename P> uint32_t add_pool(std::string_view key)
  {
    if (key.empty() || find_pool(key)) {
      fprintf(stderr, "litestl::attr: PoolStore::add_pool: empty or duplicate key\n");
      abort();
    }
    auto p = util::make_unique<P>("attr::InternPool", key);
    pools_.append(util::UniquePtr<PoolBase>(std::move(p)));
    canon_version_ = 0;
    return uint32_t(pools_.size());
  }
  uint32_t pool_count() const
  {
    return uint32_t(pools_.size());
  }
  /** Pool id by key, 0 if absent. */
  uint32_t find_pool(std::string_view key) const;
  const PoolBase &pool_any(uint32_t id) const
  {
    return *pools_[int(id) - 1];
  }
  template <typename P> const P &pool(uint32_t id) const
  {
    return static_cast<const P &>(checked<P>(id));
  }

  /* ---- entries ---- */
  /** Interns v into pool id; inner refs must be live in this store (abort). */
  template <typename P> PoolRef intern(uint32_t id, typename P::value_type v)
  {
    P &p = const_cast<P &>(static_cast<const P &>(checked<P>(id)));
    PoolTraits<typename P::value_type>::for_each_ref(v, [&](PoolRef r) {
      if (!well_formed(r) || (!r.none() && !live(r))) {
        fprintf(stderr, "litestl::attr: PoolStore::intern: inner ref is not live in this store\n");
        abort();
      }
    });
    bool miss = false;
    const uint32_t s = p.intern(std::move(v), &miss);
    if (miss) {
      version_ = next_version();
    }
    return PoolRef{id, s};
  }
  template <typename P> PoolRef find(uint32_t id, const typename P::value_type &v) const
  {
    const int64_t s = pool<P>(id).find(v);
    return s < 0 ? kNoRef : PoolRef{id, uint32_t(s)};
  }
  /** Checked: aborts on a dead slot or wrong pool class. */
  template <typename P> const typename P::value_type &get(PoolRef r) const
  {
    const typename P::value_type *v = try_get<P>(r);
    if (!v) {
      fprintf(stderr, "litestl::attr: PoolStore::get: ref {%u, %u} is not a live entry of this pool type\n",
              r.pool, r.slot);
      abort();
    }
    return *v;
  }
  /** Null for none, dead slots and wrong pool classes. */
  template <typename P> const typename P::value_type *try_get(PoolRef r) const
  {
    if (!live(r) || pools_[int(r.pool) - 1]->class_tag() != P::static_tag()) {
      return nullptr;
    }
    return &static_cast<const P &>(*pools_[int(r.pool) - 1]).get(r.slot);
  }
  bool live(PoolRef r) const
  {
    return r.pool != 0 && r.pool <= pools_.size() && pools_[int(r.pool) - 1]->live(r.slot);
  }

  /** Changes, to a never-before-seen value, on an intern miss, collect (if
   * anything was freed), canonicalize, read and import miss. */
  uint64_t version() const
  {
    return version_;
  }
  uint64_t taken_since_collect() const;
  uint64_t live_count() const;

  /* ---- liveness ---- */
  /** Frees every entry unreachable from roots. Fails without changes if a root
   * or inner ref is malformed, dangling or refused by accept. */
  bool collect(const Roots &roots, const char **err = nullptr);
  /** Read-only consistency check (see P1b §3.7). */
  bool audit(const Roots &roots, bool require_reachable, util::Vector<util::string> *problems = nullptr) const;
  /** Renumbers to canonical first-reference order (implies collect).
   * *changed reports whether any slot moved or was dropped. */
  bool canonicalize(const MutRoots &roots, bool *changed = nullptr, const char **err = nullptr);
  /** True while neither the store nor any root group changed since the last
   * canonicalize with these groups. */
  bool canonical(const Roots &roots) const;
  /** Test quarantine: freed slots are never reused, so dangling refs stay
   * detectable (canonicalize still compacts them away). */
  void set_reuse_freed(bool reuse)
  {
    quarantine_ = !reuse;
  }

  /* ---- transfer between stores ---- */
  /** Deep re-intern of src's entry r (pools matched by key). kNoRef with *err
   * on failure (dead ref, missing key, class mismatch, cycle). */
  PoolRef import(const PoolStore &src, PoolRef r, ImportMemo &memo, const char **err = nullptr);
  /** Rewrites dst's refs in rows, which hold raw refs of src (e.g. after a
   * cross-owner mix_from), into refs of this store. */
  bool import_rows(ColumnBase &dst,
                   util::span<const uint32_t> rows,
                   const PoolStore &src,
                   ImportMemo &memo,
                   const char **err = nullptr);

  /* ---- canonical hashing without canonicalising ---- */
  /** Builds the map for roots.groups (extra roots don't affect it). Fails,
   * leaving out empty, on a malformed, dangling or refused ref. */
  bool canonical_map(const Roots &roots, CanonicalMap &out, const char **err = nullptr) const;
  /** Equals hash_into(h, roots) of a copy canonicalised with the same groups.
   * Pair with AttrGroup::hash_into(h, keep, map) for the groups. */
  void hash_into(hash::Hasher128 &h, const CanonicalMap &m) const;

  /* ---- canonical hashing and I/O (require canonical(roots)) ---- */
  void hash_into(hash::Hasher128 &h, const Roots &roots) const;
  hash::Hash128 content_hash(const Roots &roots) const;
  bool write(io::ByteWriter &w, const Roots &roots) const;
  /** Into a store whose registered pools are all empty; the stream's pools
   * must match the first pools by key and traits version. */
  bool read(io::ByteReader &r, const PoolReadLimits &lim = {}, const char **err = nullptr);

private:
  template <typename P> const PoolBase &checked(uint32_t id) const
  {
    if (id == 0 || id > pools_.size() || pools_[int(id) - 1]->class_tag() != P::static_tag()) {
      fprintf(stderr, "litestl::attr: PoolStore: pool id %u is not of the requested class\n", id);
      abort();
    }
    return *pools_[int(id) - 1];
  }

  util::Vector<util::UniquePtr<PoolBase>> pools_;
  uint64_t version_;
  bool quarantine_ = false;
  /* canonical state (B1) */
  uint64_t canon_version_ = 0;
  util::Vector<uint64_t> canon_groups_;
  util::Vector<uint32_t> canon_k_;
};

} // namespace litestl::attr
