#pragma once

/* Shared fixtures for the value-pool tests (P1b): a variable-size entry type
 * with an inner ref, a custom column class holding two refs per element, a
 * journal-like root holding captured element bytes, and a test owner. */

#include "../attr/pool_store.h"
#include "../attr/registry.h"

#include <cstring>
#include <string>
#include <vector>

namespace pooltest {

using namespace litestl;
using namespace litestl::attr;

/* Variable-size entry with an optional inner ref (an approximation cache
 * pointing at its source, say). */
struct Polyline {
  util::Vector<math::double3> pts;
  PoolRef source;

  static constexpr bool has_refs = true;

  void hash_into(hash::Hasher128 &h) const
  {
    h.u32(uint32_t(pts.size()));
    for (const math::double3 &p : pts) {
      h.raw_bytes(&p, sizeof(p));
    }
    h.u32(source.pool).u32(source.slot);
  }
  bool equal(const Polyline &b) const
  {
    return pts.size() == b.pts.size() && source == b.source &&
           (pts.size() == 0 || memcmp(pts.data(), b.pts.data(), sizeof(math::double3) * size_t(pts.size())) == 0);
  }
  void write(io::ByteWriter &w) const
  {
    w.u32(uint32_t(pts.size()));
    if (pts.size()) {
      w.put(pts.data(), sizeof(math::double3) * size_t(pts.size()));
    }
    w.u32(source.pool);
    w.u32(source.slot);
  }
  static bool read(io::ByteReader &r, Polyline &v, const PoolReadLimits &)
  {
    const uint32_t n = r.u32();
    if (r.failed() || uint64_t(n) * sizeof(math::double3) > r.remaining()) {
      return false;
    }
    v.pts.resize(int(n));
    if (n) {
      r.get(v.pts.data(), sizeof(math::double3) * n);
    }
    v.source.pool = r.u32();
    v.source.slot = r.u32();
    return !r.failed();
  }
  template <typename F> void for_each_ref(F &&f) const
  {
    f(source);
  }
  template <typename F> void remap_refs(F &&f)
  {
    source = f(source);
  }
};

template <int S> using LinePoolT = InternPool<Polyline, S>;
using LinePool = LinePoolT<kDefaultSlotShift>;
using NamePool = InternPool<util::string>;
using IdPool = InternPool<uint64_t>;

/* Custom column holding two refs per element (open-extension path, B3). */
struct RefPair {
  PoolRef a, b;
};
struct RefPairOps : ValueOps<RefPair> {
  static std::string key()
  {
    return "test.refpair";
  }
};

class RefPairColumn : public Column<RefPair, RefPairOps> {
public:
  using Column::Column;
  static const ColumnType &static_type()
  {
    static const ColumnType *t = ColumnTypeRegistry::global().add_or_die<RefPairColumn>();
    return *t;
  }
  static std::string type_key()
  {
    return RefPairOps::key();
  }
  static util::UniquePtr<ColumnBase> create(const uint8_t *d, int shift)
  {
    RefPair p;
    memcpy(&p, d, sizeof(p));
    return util::UniquePtr<ColumnBase>(util::make_unique<RefPairColumn>("RefPairColumn", p, shift));
  }
  const ColumnType &type() const override
  {
    return static_type();
  }
  util::UniquePtr<ColumnBase> share() const override
  {
    auto c = util::make_unique<RefPairColumn>("RefPairColumn", store.default_value(), int(store.page_shift()));
    c->store = store.share();
    c->set_flags(flags());
    return util::UniquePtr<ColumnBase>(std::move(c));
  }
  util::UniquePtr<ColumnBase> new_like(uint32_t n) const override
  {
    auto c = util::make_unique<RefPairColumn>("RefPairColumn", store.default_value(), int(store.page_shift()));
    c->set_flags(flags());
    c->store.resize(n);
    return util::UniquePtr<ColumnBase>(std::move(c));
  }

  bool holds_refs() const override
  {
    return true;
  }
  void visit_refs(RefVisitor &v) const override
  {
    v.visit(store.default_value().a, 0);
    v.visit(store.default_value().b, 0);
    for (uint32_t i = 0; i < size(); i++) {
      v.visit(get(i).a, 1);
      v.visit(get(i).b, 1);
    }
  }
  void remap_refs(const RefRemap &m) override
  {
    for (uint32_t i = 0; i < size(); i++) {
      remap_one(i, m);
    }
  }
  void remap_refs_rows(const RefRemap &m, util::span<const uint32_t> rows) override
  {
    for (uint32_t i : rows) {
      remap_one(i, m);
    }
  }
  void visit_elem_refs(const uint8_t *e, RefVisitor &v) const override
  {
    RefPair p;
    memcpy(&p, e, sizeof(p));
    v.visit(p.a, 1);
    v.visit(p.b, 1);
  }
  void remap_elem_refs(uint8_t *e, const RefRemap &m) const override
  {
    RefPair p;
    memcpy(&p, e, sizeof(p));
    p.a = m.map(p.a);
    p.b = m.map(p.b);
    memcpy(e, &p, sizeof(p));
  }

private:
  void remap_one(uint32_t i, const RefRemap &m)
  {
    const RefPair p = get(i);
    const RefPair q{m.map(p.a), m.map(p.b)};
    if (!(q.a == p.a) || !(q.b == p.b)) {
      set(i, q);
    }
  }
};

/* Journal-like root: captured element bytes of one column type (B3). */
class JournalRoot final : public RefRoot {
public:
  const ColumnBase *proto = nullptr; /* column whose element hooks decode the bytes */
  std::vector<std::vector<uint8_t>> captures;

  void capture(const ColumnBase &c, uint32_t i)
  {
    proto = &c;
    std::vector<uint8_t> b(c.elem_size());
    c.get_bytes(i, b.data());
    captures.push_back(std::move(b));
  }
  void visit_refs(RefVisitor &v) const override
  {
    for (const auto &b : captures) {
      proto->visit_elem_refs(b.data(), v);
    }
  }
  void remap_refs(const RefRemap &m) override
  {
    for (auto &b : captures) {
      proto->remap_elem_refs(b.data(), m);
    }
  }
};

/* Test owner: a store (lines, names, ids) and two groups. Default copy and
 * move: nothing points into the store. SlotShift: the lines pool's slot page
 * shift (tests use tiny pages). */
template <int SlotShift = kDefaultSlotShift> struct OwnerT {
  using Lines = LinePoolT<SlotShift>;
  PoolStore store;
  AttrGroup a, b;
  uint32_t lines = 0, names = 0, ids = 0;

  explicit OwnerT(int page_shift = kDefaultPageShift, bool with_lines = true) : a(page_shift), b(page_shift)
  {
    if (with_lines) {
      lines = store.add_pool<Lines>("test.lines");
    }
    names = store.add_pool<NamePool>("test.names");
    ids = store.add_pool<IdPool>("test.ids");
    a.add_builtin<Column<PoolRef>>(".a.curve");
    a.add_builtin<Column<float>>(".a.w");
    b.add_builtin<Column<PoolRef>>(".b.name");
  }

  const AttrGroup *cg[2];
  AttrGroup *mg[2];
  PoolStore::Roots roots(util::span<RefRoot *const> extra = {})
  {
    cg[0] = &a;
    cg[1] = &b;
    return PoolStore::Roots{util::span<const AttrGroup *const>(cg, 2), extra, nullptr};
  }
  PoolStore::MutRoots mroots(util::span<RefRoot *const> extra = {})
  {
    mg[0] = &a;
    mg[1] = &b;
    return PoolStore::MutRoots{util::span<AttrGroup *const>(mg, 2), extra, nullptr};
  }

  Column<PoolRef> &curve()
  {
    return a.builtin<Column<PoolRef>>(0);
  }
  Column<PoolRef> &name()
  {
    return b.builtin<Column<PoolRef>>(0);
  }

  PoolRef line(std::initializer_list<double> xs, PoolRef src = kNoRef)
  {
    Polyline p;
    for (double x : xs) {
      p.pts.append(math::double3(x, x * 2, -x));
    }
    p.source = src;
    return store.intern<Lines>(lines, std::move(p));
  }
  PoolRef nm(const char *s)
  {
    return store.intern<NamePool>(names, util::string(s));
  }

  /** Owner write: canonicalize a copy, write store then groups. */
  bool write(io::ByteWriter &w) const
  {
    OwnerT c(*this);
    if (!c.store.canonicalize(c.mroots())) {
      return false;
    }
    if (!c.store.write(w, c.roots())) {
      return false;
    }
    c.a.write(w);
    c.b.write(w);
    return true;
  }
  /** Owner read into a fresh owner; strict (canonical) streams only. */
  static bool read(io::ByteReader &r, OwnerT &out, const PoolReadLimits &plim = {}, const ReadLimits &glim = {})
  {
    OwnerT f(out.a.page_shift(), out.lines != 0);
    if (!f.store.read(r, plim) || !f.a.read(r, glim) || !f.b.read(r, glim)) {
      return false;
    }
    if (!f.store.audit(f.roots(), true)) {
      return false;
    }
    bool changed = true;
    if (!f.store.canonicalize(f.mroots(), &changed) || changed) {
      return false;
    }
    out = std::move(f);
    return true;
  }

  OwnerT(const OwnerT &o) : store(o.store), a(o.a), b(o.b), lines(o.lines), names(o.names), ids(o.ids) {}
  OwnerT &operator=(const OwnerT &o)
  {
    store = o.store;
    a = o.a;
    b = o.b;
    lines = o.lines;
    names = o.names;
    ids = o.ids;
    return *this;
  }
  OwnerT(OwnerT &&o) noexcept = default;
  OwnerT &operator=(OwnerT &&o) noexcept = default;
};
using Owner = OwnerT<>;

} // namespace pooltest
