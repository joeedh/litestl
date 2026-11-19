#include "attr/pool_store.h"

#include <string>

namespace litestl::attr {

namespace {
constexpr uint32_t kStoreFormat = 1;
constexpr uint32_t kUnset = 0xffffffffu;

bool hashed(const ColumnBase &c)
{
  return !has_flag(c.flags(), ColumnFlags::Temp) && !has_flag(c.flags(), ColumnFlags::Derived);
}

uint64_t memo_key(PoolRef r)
{
  return (uint64_t(r.pool) << 32) | r.slot;
}

/* Mark bits per pool, plus the worklist for the inner-ref closure. */
struct Marks final : MarkQuery {
  util::Vector<util::Vector<uint8_t>> bits;
  bool marked(PoolRef r) const override
  {
    return !r.none() && r.pool <= bits.size() && r.slot < uint32_t(bits[int(r.pool) - 1].size()) &&
           bits[int(r.pool) - 1][int(r.slot)];
  }
};

std::string ref_str(PoolRef r)
{
  return "{" + std::to_string(r.pool) + ", " + std::to_string(r.slot) + "}";
}

std::string site_str(const RefSite &s)
{
  switch (s.kind) {
    case RefSite::Column:
      return "group " + std::to_string(s.a) + " column " + std::to_string(s.b);
    case RefSite::Inner:
      return "entry {" + std::to_string(s.a) + ", " + std::to_string(s.b) + "}";
    default:
      return "extra root " + std::to_string(s.a);
  }
}

/* Validating marker shared by collect, audit and canonicalize. */
class Marker final : public RefVisitor {
public:
  const util::Vector<util::UniquePtr<PoolBase>> &pools;
  Marks &marks;
  const PoolStore::AcceptFn *accept;
  util::Vector<util::string> *problems; /* collect all (audit) or stop at first */
  std::string first_error;
  RefSite site;
  util::Vector<PoolRef> work;

  Marker(const util::Vector<util::UniquePtr<PoolBase>> &p, Marks &m, const PoolStore::AcceptFn *acc,
         util::Vector<util::string> *probs)
      : pools(p), marks(m), accept(acc), problems(probs)
  {
    marks.bits.resize(pools.size());
    for (int i = 0; i < pools.size(); i++) {
      marks.bits[i].resize(int(pools[i]->slot_count()));
      for (int s = 0; s < marks.bits[i].size(); s++) {
        marks.bits[i][s] = 0;
      }
    }
  }

  bool failed() const
  {
    return !first_error.empty();
  }

  void fail(const std::string &m)
  {
    if (first_error.empty()) {
      first_error = m;
    }
    if (problems) {
      problems->append(util::string(m.c_str()));
    }
  }

  void visit(PoolRef r, uint64_t count) override
  {
    if (failed() && !problems) {
      return;
    }
    if (count == 0) {
      if (!r.none()) {
        fail(site_str(site) + ": ref column default is not kNoRef");
      }
      return;
    }
    if (!well_formed(r)) {
      fail(site_str(site) + ": malformed ref " + ref_str(r));
      return;
    }
    if (r.none()) {
      return;
    }
    if (r.pool > pools.size() || !pools[int(r.pool) - 1]->live(r.slot)) {
      fail(site_str(site) + ": dangling ref " + ref_str(r));
      return;
    }
    if (accept && !(*accept)(site, r)) {
      fail(site_str(site) + ": ref " + ref_str(r) + " refused by accept()");
      return;
    }
    uint8_t &b = marks.bits[int(r.pool) - 1][int(r.slot)];
    if (!b) {
      b = 1;
      work.append(r);
    }
  }

  void drain()
  {
    for (int i = 0; i < work.size(); i++) {
      const PoolRef r = work[i];
      const PoolBase &p = *pools[int(r.pool) - 1];
      if (!p.has_refs()) {
        continue;
      }
      const RefSite saved = site;
      site = RefSite{RefSite::Inner, r.pool, r.slot};
      p.for_each_inner_ref(r.slot, [&](PoolRef ir) { visit(ir, 1); });
      site = saved;
    }
    work.clear();
  }

  template <typename G> void visit_groups(util::span<G *const> groups)
  {
    for (size_t gi = 0; gi < groups.size(); gi++) {
      const AttrGroup &g = *groups[gi];
      for (uint32_t ci = 0; ci < g.column_count(); ci++) {
        const ColumnBase &c = g.column(ci);
        if (c.holds_refs()) {
          site = RefSite{RefSite::Column, uint32_t(gi), ci};
          c.visit_refs(*this);
        }
      }
    }
  }

  void visit_extra(util::span<RefRoot *const> extra)
  {
    for (size_t i = 0; i < extra.size(); i++) {
      site = RefSite{RefSite::Extra, uint32_t(i), 0};
      extra[i]->visit_refs(*this);
    }
  }

  /* weak pairs to a fixpoint (after the strong marks are drained) */
  void visit_weak(util::span<RefRoot *const> extra)
  {
    struct V final : WeakPairVisitor {
      Marker &m;
      bool any = false;
      explicit V(Marker &mm) : m(mm) {}
      void visit(PoolRef k, PoolRef v) override
      {
        if (m.marks.marked(k) && !v.none() && !m.marks.marked(v)) {
          m.visit(v, 1);
          any = true;
        }
      }
    } vis(*this);
    do {
      vis.any = false;
      for (size_t i = 0; i < extra.size(); i++) {
        site = RefSite{RefSite::Extra, uint32_t(i), 0};
        extra[i]->visit_weak_pairs(vis);
      }
      drain();
    } while (vis.any && !failed());
  }
};

/* Iterative three-colour DFS over inner refs; false on a cycle. Refs must be
 * in range (checked by the caller). */
bool acyclic(const util::Vector<util::UniquePtr<PoolBase>> &pools)
{
  util::Vector<util::Vector<uint8_t>> colour;
  colour.resize(pools.size());
  for (int i = 0; i < pools.size(); i++) {
    colour[i].resize(int(pools[i]->slot_count()));
    for (int s = 0; s < colour[i].size(); s++) {
      colour[i][s] = 0;
    }
  }
  struct Frame {
    PoolRef r;
    util::Vector<PoolRef> kids;
    int next = 0;
  };
  util::Vector<Frame> stack;
  auto push = [&](PoolRef r) {
    Frame f;
    f.r = r;
    pools[int(r.pool) - 1]->for_each_inner_ref(r.slot, [&](PoolRef k) {
      if (!k.none()) {
        f.kids.append(k);
      }
    });
    colour[int(r.pool) - 1][int(r.slot)] = 1;
    stack.append(std::move(f));
  };
  for (int pi = 0; pi < pools.size(); pi++) {
    if (!pools[pi]->has_refs()) {
      continue;
    }
    for (uint32_t s = 0; s < pools[pi]->slot_count(); s++) {
      if (!pools[pi]->live(s) || colour[pi][int(s)]) {
        continue;
      }
      push(PoolRef{uint32_t(pi + 1), s});
      while (stack.size()) {
        Frame &f = stack.last();
        if (f.next == f.kids.size()) {
          colour[int(f.r.pool) - 1][int(f.r.slot)] = 2;
          stack.pop_back();
          continue;
        }
        const PoolRef k = f.kids[f.next++];
        const uint8_t c = colour[int(k.pool) - 1][int(k.slot)];
        if (c == 1) {
          return false;
        }
        if (c == 0) {
          push(k);
        }
      }
    }
  }
  return true;
}

/* The first n pools with k[i] entries each (n = the stream's pool count). */
void hash_pools(hash::Hasher128 &h,
                const util::Vector<util::UniquePtr<PoolBase>> &pools,
                const util::Vector<uint32_t> &k,
                uint32_t n)
{
  h.u32(n);
  for (int i = 0; i < int(n); i++) {
    h.str(pools[i]->key());
    h.u32(pools[i]->traits_version());
    h.u32(k[i]);
    for (uint32_t s = 0; s < k[i]; s++) {
      h.h128(pools[i]->entry_hash(s));
    }
  }
}

/* Maps through per-pool old->new tables (canonicalize). */
class TableRemap final : public RefRemap {
public:
  const util::Vector<util::Vector<uint32_t>> &old_to_new;
  explicit TableRemap(const util::Vector<util::Vector<uint32_t>> &t) : old_to_new(t) {}
  PoolRef map(PoolRef r) const override
  {
    if (r.none()) {
      return r;
    }
    return PoolRef{r.pool, old_to_new[int(r.pool) - 1][int(r.slot)]};
  }
};

/* Numbering in order of first reference; the queue is drained (BFS) through
 * inner refs per phase (canonicalize, canonical_map). */
class Numberer final : public RefVisitor {
public:
  const util::Vector<util::UniquePtr<PoolBase>> &pools;
  util::Vector<util::Vector<uint32_t>> &o2n, &n2o;
  util::Vector<PoolRef> queue;
  int qpos = 0;
  Numberer(const util::Vector<util::UniquePtr<PoolBase>> &p,
           util::Vector<util::Vector<uint32_t>> &a,
           util::Vector<util::Vector<uint32_t>> &b)
      : pools(p), o2n(a), n2o(b)
  {
    o2n.resize(pools.size());
    n2o.resize(pools.size());
    for (int i = 0; i < pools.size(); i++) {
      o2n[i].resize(int(pools[i]->slot_count()));
      for (int s = 0; s < o2n[i].size(); s++) {
        o2n[i][s] = kUnset;
      }
    }
  }
  void visit(PoolRef r, uint64_t count) override
  {
    if (count == 0 || r.none()) {
      return;
    }
    uint32_t &n = o2n[int(r.pool) - 1][int(r.slot)];
    if (n == kUnset) {
      n = uint32_t(n2o[int(r.pool) - 1].size());
      n2o[int(r.pool) - 1].append(r.slot);
      queue.append(r);
    }
  }
  void drain()
  {
    while (qpos < queue.size()) {
      const PoolRef r = queue[qpos++];
      if (pools[int(r.pool) - 1]->has_refs()) {
        pools[int(r.pool) - 1]->for_each_inner_ref(r.slot, [&](PoolRef ir) { visit(ir, 1); });
      }
    }
  }
  /* the hashed (phase 1) or unhashed (phase 2) ref columns of the groups */
  template <typename G> void visit_groups(util::span<G *const> groups, bool want_hashed)
  {
    for (const AttrGroup *g : groups) {
      for (int ci : g->canonical_order()) {
        const ColumnBase &c = g->column(uint32_t(ci));
        if (c.holds_refs() && hashed(c) == want_hashed) {
          c.visit_refs(*this);
        }
      }
    }
    drain();
  }
  bool numbered(PoolRef r) const
  {
    return !r.none() && o2n[int(r.pool) - 1][int(r.slot)] != kUnset;
  }
  /* weak pairs to a fixpoint (refs already validated by a Marker) */
  void visit_weak(util::span<RefRoot *const> extra)
  {
    struct V final : WeakPairVisitor {
      Numberer &n;
      bool any = false;
      explicit V(Numberer &nn) : n(nn) {}
      void visit(PoolRef k, PoolRef v) override
      {
        if (n.numbered(k) && !v.none() && !n.numbered(v)) {
          n.visit(v, 1);
          any = true;
        }
      }
    } vis(*this);
    do {
      vis.any = false;
      for (RefRoot *r : extra) {
        r->visit_weak_pairs(vis);
      }
      drain();
    } while (vis.any);
  }
};

/* The numbered set as mark state (canonicalize's prune). */
class NumberedQuery final : public MarkQuery {
public:
  const Numberer &n;
  explicit NumberedQuery(const Numberer &nn) : n(nn) {}
  bool marked(PoolRef r) const override
  {
    return n.numbered(r);
  }
};

/* Maps src refs to dst refs through an import memo (entries must exist). */
class MemoRemap final : public RefRemap {
public:
  util::Map<uint64_t, PoolRef> &memo;
  explicit MemoRemap(util::Map<uint64_t, PoolRef> &m) : memo(m) {}
  PoolRef map(PoolRef r) const override
  {
    if (r.none()) {
      return r;
    }
    const PoolRef *d = memo.lookup_ptr(memo_key(r));
    return d ? *d : kNoRef;
  }
};

class Collector final : public RefVisitor {
public:
  util::Vector<PoolRef> refs;
  void visit(PoolRef r, uint64_t count) override
  {
    if (count && !r.none()) {
      refs.append(r);
    }
  }
};
} // namespace

/* ------------------------------------------------------------------------ */

PoolStore::PoolStore() : version_(next_version()) {}

PoolStore::PoolStore(const PoolStore &b)
    : version_(b.version_),
      quarantine_(b.quarantine_),
      canon_version_(b.canon_version_),
      canon_groups_(b.canon_groups_),
      canon_k_(b.canon_k_)
{
  for (const auto &p : b.pools_) {
    pools_.append(p->clone());
  }
}

PoolStore &PoolStore::operator=(const PoolStore &b)
{
  if (this != &b) {
    PoolStore tmp(b);
    *this = std::move(tmp);
  }
  return *this;
}

uint32_t PoolStore::find_pool(std::string_view key) const
{
  for (int i = 0; i < pools_.size(); i++) {
    if (pools_[i]->key() == key) {
      return uint32_t(i + 1);
    }
  }
  return 0;
}

uint64_t PoolStore::taken_since_collect() const
{
  uint64_t n = 0;
  for (const auto &p : pools_) {
    n += p->taken_since_collect();
  }
  return n;
}

uint64_t PoolStore::live_count() const
{
  uint64_t n = 0;
  for (const auto &p : pools_) {
    n += p->live_count();
  }
  return n;
}

/* ---- liveness ---------------------------------------------------------- */

bool PoolStore::collect(const Roots &roots, const char **err)
{
  Marks marks;
  Marker m(pools_, marks, roots.accept, nullptr);
  m.visit_groups(roots.groups);
  m.drain();
  m.visit_extra(roots.extra);
  m.drain();
  m.visit_weak(roots.extra);
  if (m.failed()) {
    if (err) {
      static thread_local std::string msg;
      msg = "collect: " + m.first_error;
      *err = msg.c_str();
    }
    return false;
  }
  for (RefRoot *r : roots.extra) {
    r->prune(marks);
  }
  bool freed = false;
  for (int pi = 0; pi < pools_.size(); pi++) {
    PoolBase &p = *pools_[pi];
    /* descending, so the lowest freed index ends on top of the LIFO free list */
    for (uint32_t s = p.slot_count(); s-- > 0;) {
      if (p.live(s) && !marks.bits[pi][int(s)]) {
        p.free_slot(s, quarantine_);
        freed = true;
      }
    }
    p.reset_taken();
  }
  if (freed) {
    version_ = next_version();
  }
  return true;
}

bool PoolStore::audit(const Roots &roots, bool require_reachable, util::Vector<util::string> *problems) const
{
  util::Vector<util::string> local;
  util::Vector<util::string> &probs = problems ? *problems : local;
  const int before = probs.size();
  Marks marks;
  Marker m(pools_, marks, roots.accept, &probs);
  m.visit_groups(roots.groups);
  m.drain();
  m.visit_extra(roots.extra);
  m.drain();
  m.visit_weak(roots.extra);
  /* inner refs of every live entry (reachable or not) must be valid */
  for (int pi = 0; pi < pools_.size(); pi++) {
    const PoolBase &p = *pools_[pi];
    for (uint32_t s = 0; s < p.slot_count(); s++) {
      if (!p.live(s)) {
        continue;
      }
      if (require_reachable && !marks.bits[pi][int(s)]) {
        probs.append(util::string(("unreachable live entry {" + std::to_string(pi + 1) + ", " +
                                   std::to_string(s) + "} in " + std::string(p.key()))
                                      .c_str()));
      }
      if (p.has_refs()) {
        p.for_each_inner_ref(s, [&](PoolRef r) {
          if (!well_formed(r) || (!r.none() && !live(r))) {
            probs.append(util::string(("entry {" + std::to_string(pi + 1) + ", " + std::to_string(s) +
                                       "}: dangling or malformed inner ref " + ref_str(r))
                                          .c_str()));
          }
        });
      }
    }
    p.audit_internal(probs);
  }
  if (probs.size() == before && !acyclic(pools_)) {
    probs.append(util::string("inner-ref cycle"));
  }
  return probs.size() == before;
}

bool PoolStore::canonicalize(const MutRoots &roots, bool *changed, const char **err)
{
  /* validate everything first: no changes on failure */
  {
    Marks marks;
    Marker m(pools_, marks, roots.accept, nullptr);
    m.visit_groups(util::span<const AttrGroup *const>(roots.groups.data(), roots.groups.size()));
    m.drain();
    m.visit_extra(roots.extra);
    m.drain();
    m.visit_weak(roots.extra);
    if (m.failed()) {
      if (err) {
        static thread_local std::string msg;
        msg = "canonicalize: " + m.first_error;
        *err = msg.c_str();
      }
      return false;
    }
  }

  const int np = pools_.size();
  util::Vector<util::Vector<uint32_t>> old_to_new, new_to_old;
  Numberer num(pools_, old_to_new, new_to_old);
  for (const bool want_hashed : {true, false}) {
    num.visit_groups(roots.groups, want_hashed);
    if (want_hashed) {
      canon_k_.resize(np);
      for (int i = 0; i < np; i++) {
        canon_k_[i] = uint32_t(new_to_old[i].size());
      }
    }
  }
  for (RefRoot *r : roots.extra) {
    r->visit_refs(num);
  }
  num.drain();
  num.visit_weak(roots.extra);
  {
    const NumberedQuery q(num);
    for (RefRoot *r : roots.extra) {
      r->prune(q);
    }
  }

  bool any_change = false;
  for (int i = 0; i < np; i++) {
    const PoolBase &p = *pools_[i];
    if (uint32_t(new_to_old[i].size()) != p.slot_count()) {
      any_change = true;
      continue;
    }
    for (int n = 0; n < new_to_old[i].size(); n++) {
      if (new_to_old[i][n] != uint32_t(n)) {
        any_change = true;
        break;
      }
    }
  }
  if (changed) {
    *changed = any_change;
  }

  if (any_change) {
    const TableRemap remap(old_to_new);
    for (AttrGroup *g : roots.groups) {
      for (uint32_t ci = 0; ci < g->column_count(); ci++) {
        if (g->column(ci).holds_refs()) {
          g->column(ci).remap_refs(remap);
        }
      }
    }
    for (RefRoot *r : roots.extra) {
      r->remap_refs(remap);
    }
    for (int i = 0; i < np; i++) {
      pools_[i]->renumber(util::span<const uint32_t>(new_to_old[i].data(), new_to_old[i].size()), remap);
    }
  }
  for (auto &p : pools_) {
    p->reset_taken();
  }
  version_ = next_version();
  canon_version_ = version_;
  canon_groups_.clear();
  for (AttrGroup *g : roots.groups) {
    canon_groups_.append(g->version());
  }
  return true;
}

bool PoolStore::canonical(const Roots &roots) const
{
  if (canon_version_ == 0 || canon_version_ != version_ || roots.groups.size() != size_t(canon_groups_.size()) ||
      canon_k_.size() != pools_.size())
  {
    return false;
  }
  for (size_t i = 0; i < roots.groups.size(); i++) {
    if (roots.groups[i]->version() != canon_groups_[int(i)]) {
      return false;
    }
  }
  return true;
}

/* ---- transfer ---------------------------------------------------------- */

PoolRef PoolStore::import(const PoolStore &src, PoolRef r, ImportMemo &memo, const char **err)
{
  auto fail = [&](const char *m) {
    if (err) {
      *err = m;
    }
    return kNoRef;
  };
  if (r.none()) {
    return kNoRef;
  }
  if (!src.live(r)) {
    return fail("import: source ref is not live");
  }
  if (const PoolRef *d = memo.map.lookup_ptr(memo_key(r))) {
    return *d;
  }
  /* iterative post-order; in-progress entries are memoized as kNoRef */
  struct Frame {
    PoolRef r;
    util::Vector<PoolRef> kids;
    int next = 0;
  };
  util::Vector<Frame> stack;
  util::Map<uint64_t, bool> in_progress;
  auto push = [&](PoolRef x) {
    Frame f;
    f.r = x;
    src.pools_[int(x.pool) - 1]->for_each_inner_ref(x.slot, [&](PoolRef k) {
      if (!k.none()) {
        f.kids.append(k);
      }
    });
    in_progress.add(memo_key(x), true);
    stack.append(std::move(f));
  };
  push(r);
  while (stack.size()) {
    Frame &f = stack.last();
    if (f.next < f.kids.size()) {
      const PoolRef k = f.kids[f.next++];
      if (memo.map.contains(memo_key(k))) {
        continue;
      }
      if (in_progress.contains(memo_key(k))) {
        return fail("import: inner-ref cycle in source");
      }
      if (!src.live(k)) {
        return fail("import: dangling inner ref in source");
      }
      push(k);
      continue;
    }
    const PoolBase &sp = *src.pools_[int(f.r.pool) - 1];
    const uint32_t id = find_pool(sp.key());
    if (id == 0) {
      return fail("import: destination has no pool with the source key");
    }
    PoolBase &dp = *pools_[int(id) - 1];
    if (dp.class_tag() != sp.class_tag()) {
      return fail("import: pool classes differ for the same key");
    }
    const MemoRemap remap(memo.map);
    bool miss = false;
    const uint32_t s = dp.intern_copy(sp, f.r.slot, remap, &miss);
    if (miss) {
      version_ = next_version();
    }
    memo.map.add_overwrite(memo_key(f.r), PoolRef{id, s});
    in_progress.remove(memo_key(f.r));
    stack.pop_back();
  }
  return *memo.map.lookup_ptr(memo_key(r));
}

bool PoolStore::import_rows(
    ColumnBase &dst, util::span<const uint32_t> rows, const PoolStore &src, ImportMemo &memo, const char **err)
{
  if (!dst.holds_refs()) {
    return true;
  }
  util::Vector<uint8_t> buf;
  buf.resize(int(dst.elem_size()));
  Collector col;
  for (uint32_t row : rows) {
    if (row >= dst.size()) {
      if (err) {
        *err = "import_rows: row out of range";
      }
      return false;
    }
    dst.get_bytes(row, buf.data());
    dst.visit_elem_refs(buf.data(), col);
  }
  for (const PoolRef r : col.refs) {
    if (!well_formed(r)) {
      if (err) {
        *err = "import_rows: malformed ref";
      }
      return false;
    }
    if (import(src, r, memo, err).none()) {
      return false;
    }
  }
  dst.remap_refs_rows(MemoRemap(memo.map), rows);
  return true;
}

/* ---- hashing and I/O --------------------------------------------------- */

void PoolStore::hash_into(hash::Hasher128 &h, const Roots &roots) const
{
  if (!canonical(roots)) {
    fprintf(stderr, "litestl::attr: PoolStore::hash_into requires canonical(roots)\n");
    abort();
  }
  hash_pools(h, pools_, canon_k_, uint32_t(pools_.size()));
}

PoolRef PoolStore::CanonicalMap::map(PoolRef r) const
{
  if (r.none()) {
    return r;
  }
  return PoolRef{r.pool, old_to_new[int(r.pool) - 1][int(r.slot)]};
}

bool PoolStore::canonical_map(const Roots &roots, CanonicalMap &out, const char **err) const
{
  out.new_to_old.clear();
  out.old_to_new.clear();
  {
    Marks marks;
    Marker m(pools_, marks, roots.accept, nullptr);
    m.visit_groups(roots.groups);
    m.drain();
    if (m.failed()) {
      if (err) {
        static thread_local std::string msg;
        msg = "canonical_map: " + m.first_error;
        *err = msg.c_str();
      }
      return false;
    }
  }
  Numberer num(pools_, out.old_to_new, out.new_to_old);
  num.visit_groups(roots.groups, true);
  return true;
}

void PoolStore::hash_into(hash::Hasher128 &h, const CanonicalMap &m) const
{
  h.u32(uint32_t(pools_.size()));
  for (int i = 0; i < pools_.size(); i++) {
    const PoolBase &p = *pools_[i];
    const util::Vector<uint32_t> &order = m.new_to_old[i];
    h.str(p.key());
    h.u32(p.traits_version());
    h.u32(uint32_t(order.size()));
    for (uint32_t s : order) {
      h.h128(p.has_refs() ? p.entry_hash_mapped(s, m) : p.entry_hash(s));
    }
  }
}

hash::Hash128 PoolStore::content_hash(const Roots &roots) const
{
  hash::Hasher128 h(hash::DomainTag::Content);
  hash_into(h, roots);
  return h.finish();
}

bool PoolStore::write(io::ByteWriter &w, const Roots &roots) const
{
  if (!canonical(roots)) {
    return false;
  }
  w.u32(kStoreFormat);
  w.u32(uint32_t(pools_.size()));
  for (int i = 0; i < pools_.size(); i++) {
    w.str(pools_[i]->key());
    w.u32(pools_[i]->traits_version());
    w.u32(canon_k_[i]);
    pools_[i]->write_entries(w, canon_k_[i]);
  }
  const hash::Hash128 h = content_hash(roots);
  w.u64(h.lo);
  w.u64(h.hi);
  return true;
}

bool PoolStore::read(io::ByteReader &r, const PoolReadLimits &lim, const char **err)
{
  auto fail = [&](const char *m) {
    if (err) {
      *err = m;
    }
    return false;
  };
  for (const auto &p : pools_) {
    if (p->slot_count() != 0) {
      return fail("pool read: the store is not empty");
    }
  }
  if (r.u32() != kStoreFormat) {
    return fail("pool read: unsupported format version");
  }
  const uint32_t n = r.u32();
  if (r.failed() || n > uint32_t(pools_.size())) {
    return fail("pool read: more pools than the store has");
  }
  util::Vector<util::UniquePtr<PoolBase>> staging;
  util::Vector<uint32_t> k;
  for (int i = 0; i < pools_.size(); i++) {
    staging.append(pools_[i]->new_empty());
    k.append(0);
  }
  for (uint32_t i = 0; i < n; i++) {
    const std::string_view key = r.str();
    const uint32_t version = r.u32();
    const uint32_t count = r.u32();
    if (r.failed() || key != pools_[int(i)]->key()) {
      return fail("pool read: pool key mismatch");
    }
    if (version != pools_[int(i)]->traits_version()) {
      return fail("pool read: entry format version mismatch");
    }
    if (count > lim.max_slots) {
      return fail("pool read: slot count exceeds limits");
    }
    if (!staging[int(i)]->read_entries(r, count, lim, err)) {
      return false;
    }
    k[int(i)] = count;
  }
  /* inner refs in range (every read slot is live) */
  for (int i = 0; i < staging.size(); i++) {
    const PoolBase &p = *staging[i];
    if (!p.has_refs()) {
      continue;
    }
    bool ok = true;
    for (uint32_t s = 0; s < p.slot_count() && ok; s++) {
      p.for_each_inner_ref(s, [&](PoolRef ir) {
        if (!well_formed(ir) ||
            (!ir.none() && (ir.pool > uint32_t(staging.size()) || ir.slot >= staging[int(ir.pool) - 1]->slot_count())))
        {
          ok = false;
        }
      });
    }
    if (!ok) {
      return fail("pool read: dangling or malformed inner ref");
    }
  }
  if (!acyclic(staging)) {
    return fail("pool read: inner-ref cycle");
  }
  hash::Hash128 h;
  h.lo = r.u64();
  h.hi = r.u64();
  if (r.failed()) {
    return fail("pool read: truncated");
  }
  if (lim.verify_hash) {
    hash::Hasher128 hh(hash::DomainTag::Content);
    hash_pools(hh, staging, k, n);
    if (!(hh.finish() == h)) {
      return fail("pool read: content hash mismatch (corrupt stream)");
    }
  }
  for (auto &p : staging) {
    p->reset_taken();
  }
  pools_ = std::move(staging);
  version_ = next_version();
  canon_version_ = 0;
  canon_groups_.clear();
  canon_k_.clear();
  return true;
}

} // namespace litestl::attr
