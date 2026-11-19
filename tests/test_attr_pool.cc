/* Value pools (P1b): interning, mark-and-sweep collection, inner refs,
 * snapshots, canonical form, serialization (including structural fuzzing past
 * the trailing hashes), import, custom ref columns and journal-like roots. */

#include "pool_test_types.h"

#include "splitmix64.h"
#include "test_harness.h"

#include <cstdio>
#include <string>
#include <vector>

test_init;

using namespace pooltest;

static std::vector<uint8_t> owner_bytes(const Owner &o)
{
  io::ByteWriter w;
  test_assert(o.write(w));
  return std::vector<uint8_t>(w.data(), w.data() + w.size());
}

static void test_intern_and_get()
{
  Owner o;
  const PoolRef r1 = o.line({1, 2, 3});
  const PoolRef r2 = o.line({1, 2, 3});
  const PoolRef r3 = o.line({1, 2});
  test_assert(r1 == r2);
  test_assert(!(r1 == r3));
  test_assert(r1.pool == o.lines);
  test_assert(o.store.get<LinePool>(r1).pts.size() == 3);
  test_assert(o.store.try_get<NamePool>(r1) == nullptr); /* wrong kind */
  test_assert(o.store.try_get<LinePool>(kNoRef) == nullptr);
  test_assert(o.store.find<LinePool>(o.lines, o.store.get<LinePool>(r3)) == r3);
  const PoolRef n1 = o.nm("asphalt");
  test_assert(o.nm("asphalt") == n1);
  test_assert(std::string_view(o.store.get<NamePool>(n1)) == "asphalt");
  /* -0.0 and +0.0 are different entries (bitwise equality) */
  const PoolRef z1 = o.store.intern<IdPool>(o.ids, 7);
  test_assert(o.store.intern<IdPool>(o.ids, 7) == z1);
  test_assert(o.store.live_count() == 4);
}

static void test_collect_basics()
{
  Owner o;
  o.a.resize(10);
  o.b.resize(4);
  const PoolRef keep = o.line({1});
  const PoolRef drop = o.line({2});
  const PoolRef drop2 = o.line({3});
  o.curve().set(3, keep);
  o.curve().fill(5, 3, keep); /* Constant-ish run */
  test_assert(o.store.taken_since_collect() == 3);
  test_assert(o.store.collect(o.roots()));
  test_assert(o.store.live(keep) && !o.store.live(drop) && !o.store.live(drop2));
  test_assert(o.store.taken_since_collect() == 0);
  /* LIFO free list, lowest freed index first */
  const PoolRef n1 = o.line({4});
  const PoolRef n2 = o.line({5});
  test_assert(n1.slot == drop.slot && n2.slot == drop2.slot);
  /* resurrection: garbage interned again before a collect is the same slot */
  o.curve().set(0, n1);
  test_assert(o.store.collect(o.roots()));
  test_assert(!o.store.live(n2));
  const PoolRef again = o.line({4});
  test_assert(again == n1);
  test_assert(o.store.audit(o.roots(), true));
  /* uniform page of refs survives collect */
  o.curve().fill(0, 10, keep);
  test_assert(o.store.collect(o.roots()));
  test_assert(o.store.live(keep) && !o.store.live(n1));
}

static void test_collect_failures()
{
  Owner o;
  o.a.resize(4);
  const PoolRef k = o.line({1});
  o.curve().set(0, k);
  o.curve().set(1, PoolRef{o.lines, 999}); /* dangling */
  const char *err = nullptr;
  test_assert(!o.store.collect(o.roots(), &err) && err);
  test_assert(o.store.live(k));
  o.curve().set(1, PoolRef{0, 5}); /* malformed */
  test_assert(!o.store.collect(o.roots(), &err));
  o.curve().set(1, PoolRef{77, 0}); /* no such pool */
  test_assert(!o.store.collect(o.roots(), &err));
  o.curve().set(1, kNoRef);
  test_assert(o.store.collect(o.roots(), &err));
  /* non-kNoRef default rejected */
  auto *bad = o.a.ensure<Column<PoolRef>>("bad", k);
  test_assert(bad != nullptr);
  test_assert(!o.store.collect(o.roots(), &err));
  util::Vector<util::string> probs;
  test_assert(!o.store.audit(o.roots(), false, &probs) && probs.size() > 0);
  o.a.remove("bad");
  test_assert(o.store.collect(o.roots()));
  /* accept() refuses names on the curve column */
  const PoolRef n = o.nm("x");
  o.curve().set(2, n);
  auto accept_fn = [&](const RefSite &s, PoolRef r) {
    return !(s.kind == RefSite::Column && s.a == 0 && s.b == 0 && r.pool != o.lines);
  };
  PoolStore::AcceptFn acc = accept_fn; /* function_ref: the lambda must outlive it */
  PoolStore::Roots rr = o.roots();
  rr.accept = &acc;
  test_assert(!o.store.collect(rr, &err));
  test_assert(!o.store.audit(rr, false));
  o.curve().set(2, kNoRef);
  test_assert(o.store.collect(rr, &err));
}

static void test_inner_refs_and_chain()
{
  Owner o;
  o.a.resize(2);
  const PoolRef base = o.line({1, 1});
  const PoolRef cache = o.line({2, 2}, base);
  o.curve().set(0, cache);
  test_assert(o.store.collect(o.roots()));
  test_assert(o.store.live(base) && o.store.live(cache));
  o.curve().set(0, kNoRef);
  test_assert(o.store.collect(o.roots()));
  test_assert(!o.store.live(base) && !o.store.live(cache));

  /* a long chain: mark and canonicalize are iterative */
  const int n = 10000;
  PoolRef prev = kNoRef;
  for (int i = 0; i < n; i++) {
    prev = o.line({double(i)}, prev);
  }
  o.curve().set(1, prev);
  test_assert(o.store.collect(o.roots()));
  test_assert(o.store.pool_any(o.lines).live_count() == uint32_t(n));
  test_assert(o.store.audit(o.roots(), true));
  test_assert(o.store.canonicalize(o.mroots()));
  test_assert(o.curve().get(1) == (PoolRef{o.lines, 0}));
  test_assert(o.store.get<LinePool>(PoolRef{o.lines, 1}).pts[0][0] == double(n - 2));
  /* import of the chain into another store is iterative too */
  Owner d;
  PoolStore::ImportMemo memo;
  const PoolRef imp = d.store.import(o.store, o.curve().get(1), memo);
  test_assert(!imp.none());
  test_assert(d.store.pool_any(d.lines).live_count() == uint32_t(n));
  o.curve().set(1, kNoRef);
  test_assert(o.store.collect(o.roots()));
  test_assert(o.store.live_count() == 0);
}

static void test_quarantine_detects_forgotten_root()
{
  Owner o;
  o.a.resize(2);
  o.store.set_reuse_freed(false);
  const PoolRef r = o.line({1});
  AttrGroup forgotten;
  forgotten.add_builtin<Column<PoolRef>>("ref");
  forgotten.resize(1);
  forgotten.builtin<Column<PoolRef>>(0).set(0, r);
  test_assert(o.store.collect(o.roots())); /* forgotten is not a root */
  test_assert(!o.store.live(r));
  const PoolRef r2 = o.line({2});
  test_assert(!(r2.slot == r.slot)); /* quarantined, not reused */
  const AttrGroup *gs[] = {&o.a, &o.b, &forgotten};
  PoolStore::Roots all{util::span<const AttrGroup *const>(gs, 3), {}, nullptr};
  test_assert(!o.store.audit(all, false)); /* the stale ref is detected */
  /* B11: canonicalize compacts quarantined slots away; with reuse the stale
   * ref would silently resolve to another entry, which is why owners must
   * root everything. */
  o.curve().set(0, r2);
  test_assert(o.store.canonicalize(o.mroots()));
  test_assert(o.store.live(forgotten.builtin<Column<PoolRef>>(0).get(0))); /* now aliases r2's entry */
}

static void test_snapshots_diverge()
{
  Owner o;
  o.a.resize(100);
  for (uint32_t i = 0; i < 100; i++) {
    o.curve().set(i, o.line({double(i)}));
  }
  Owner s1(o), s2(o);
  /* diverge */
  s1.curve().set(5, s1.line({500}));
  s2.curve().set(5, kNoRef);
  s2.curve().set(6, s2.line({600}));
  test_assert(s1.store.collect(s1.roots()));
  test_assert(s2.store.collect(s2.roots()));
  test_assert(o.store.collect(o.roots()));
  test_assert(s1.store.get<LinePool>(s1.curve().get(5)).pts[0][0] == 500);
  test_assert(s2.store.get<LinePool>(s2.curve().get(6)).pts[0][0] == 600);
  test_assert(o.store.get<LinePool>(o.curve().get(5)).pts[0][0] == 5);
  test_assert(o.store.get<LinePool>(o.curve().get(6)).pts[0][0] == 6);
  /* s2 freed slot of value 5 and reused it for 600; o is unaffected */
  test_assert(o.store.audit(o.roots(), true) && s1.store.audit(s1.roots(), true) &&
              s2.store.audit(s2.roots(), true));
  /* canonicalizing one snapshot leaves the others intact */
  test_assert(s2.store.canonicalize(s2.mroots()));
  test_assert(o.store.get<LinePool>(o.curve().get(6)).pts[0][0] == 6);
  test_assert(o.store.audit(o.roots(), true));
}

static void test_canonical_form()
{
  /* same hashed content, different histories */
  auto build = [](int variant) {
    Owner o;
    o.a.resize(50);
    o.b.resize(8);
    test::SplitMix64 rng(uint64_t(variant) * 7919 + 1);
    if (variant == 1) {
      for (int i = 0; i < 30; i++) {
        o.line({1000.0 + i}); /* garbage */
      }
    }
    std::vector<int> order(50);
    for (int i = 0; i < 50; i++) {
      order[i] = variant == 0 ? i : 49 - i;
    }
    for (int i : order) {
      if (i % 3) {
        const PoolRef src = o.line({double(i % 5)});
        o.curve().set(uint32_t(i), o.line({double(i), double(i)}, src));
      }
      if (variant == 2 && rng.coin()) {
        o.store.collect(o.roots());
      }
    }
    for (uint32_t i = 0; i < 8; i++) {
      o.name().set(i, o.nm(i % 2 ? "concrete" : "glass"));
    }
    /* Temp column and extra roots referencing other entries */
    if (variant >= 1) {
      auto *t = o.a.ensure<Column<PoolRef>>("scratch", kNoRef, ColumnFlags::Temp);
      for (uint32_t i = 0; i < 50; i += 7) {
        t->set(i, o.line({-double(i)}, o.line({double(i % 5)})));
      }
    }
    return o;
  };
  Owner o0 = build(0), o1 = build(1), o2 = build(2);
  Owner sibling(o1);
  sibling.curve().set(1, sibling.line({12345}));
  const auto b0 = owner_bytes(o0), b1 = owner_bytes(o1), b2 = owner_bytes(o2);
  test_assert(b0 == b1);
  test_assert(b0 == b2);
  /* extra root and journal do not change the bytes */
  JournalRoot j;
  j.capture(o1.curve(), 4);
  Owner c(o1);
  RefRoot *ex[] = {&j};
  test_assert(c.store.canonicalize(c.mroots(util::span<RefRoot *const>(ex, 1))));
  /* store hashes agree too */
  Owner c0(o0), c1(o1);
  test_assert(c0.store.canonicalize(c0.mroots()) && c1.store.canonicalize(c1.mroots()));
  test_assert(c0.store.content_hash(c0.roots()) == c1.store.content_hash(c1.roots()));

  /* canonical_map: the hash a canonicalised copy gives, without touching the
   * owner; independent of history and of unreferenced entries */
  {
    auto all = [](std::string_view, const ColumnBase &) { return true; };
    auto map_hash = [&](Owner &o) {
      PoolStore::CanonicalMap m;
      test_assert(o.store.canonical_map(o.roots(), m));
      hash::Hasher128 h(hash::DomainTag::Content);
      o.store.hash_into(h, m);
      o.a.hash_into(h, all, m);
      o.b.hash_into(h, all, m);
      return h.finish();
    };
    auto copy_hash = [&](const Owner &o) {
      Owner c(o);
      test_assert(c.store.canonicalize(c.mroots()));
      hash::Hasher128 h(hash::DomainTag::Content);
      c.store.hash_into(h, c.roots());
      c.a.hash_into(h);
      c.b.hash_into(h);
      return h.finish();
    };
    for (Owner *o : {&o0, &o1, &o2, &sibling}) {
      const uint64_t sv = o->store.version(), av = o->a.version();
      test_assert(map_hash(*o) == copy_hash(*o));
      test_assert(o->store.version() == sv && o->a.version() == av);
    }
    test_assert(map_hash(o0) == map_hash(o1) && map_hash(o0) == map_hash(o2));
    test_assert(!(map_hash(o0) == map_hash(sibling)));
    Owner extra(o0);
    extra.line({999.0}, extra.line({998.0})); /* unreferenced */
    test_assert(map_hash(extra) == map_hash(o0));
    Owner bad(o0);
    bad.curve().set(3, PoolRef{0, 5}); /* malformed */
    PoolStore::CanonicalMap m;
    test_assert(!bad.store.canonical_map(bad.roots(), m));
    /* map is total: a ref outside the map (here, any ref of the map a failed
     * canonical_map leaves) gives an unset slot, never an out-of-range read */
    test_assert(m.map(PoolRef{1, 3}).pool == 1 && m.map(PoolRef{1, 3}).slot == 0xffffffffu);
    test_assert(m.map(PoolRef{200, 0}).slot == 0xffffffffu && m.map(kNoRef).none());
  }

  /* B1: after canonicalize, intern-hit a Temp-only entry and assign it into a
   * hashed column: write must refuse (groups changed). */
  Owner w(o1);
  test_assert(w.store.canonicalize(w.mroots()));
  io::ByteWriter ok;
  test_assert(w.store.write(ok, w.roots()));
  const PoolRef temp_only = w.line({-7.0}, w.line({2.0}));
  test_assert(w.store.canonical(w.roots())); /* a hit changes nothing yet */
  w.curve().set(0, temp_only);
  test_assert(!w.store.canonical(w.roots()));
  io::ByteWriter bad;
  test_assert(!w.store.write(bad, w.roots()));
}

static void test_round_trip_and_read_failures()
{
  Owner o;
  o.a.resize(20);
  o.b.resize(3);
  for (uint32_t i = 0; i < 20; i += 2) {
    o.curve().set(i, o.line({double(i)}, i % 4 ? o.line({0.5}) : kNoRef));
  }
  o.name().set(1, o.nm("steel"));
  const auto bytes = owner_bytes(o);
  Owner back;
  {
    io::ByteReader r(bytes.data(), bytes.size());
    test_assert(Owner::read(r, back));
  }
  test_assert(owner_bytes(back) == bytes);
  test_assert(std::string_view(back.store.get<NamePool>(back.name().get(1))) == "steel");

  /* key mismatch: an owner whose first pool is names */
  {
    Owner other(kDefaultPageShift, false);
    io::ByteReader r(bytes.data(), bytes.size());
    test_assert(!Owner::read(r, other));
  }
  /* truncation at every prefix length */
  for (size_t n = 0; n < bytes.size(); n += (n < 64 ? 1 : 7)) {
    Owner t;
    io::ByteReader r(bytes.data(), n);
    test_assert(!Owner::read(r, t));
  }
  /* read into a non-empty store */
  {
    Owner t;
    t.line({1});
    io::ByteReader r(bytes.data(), bytes.size());
    test_assert(!t.store.read(r));
  }

  /* crafted streams (hash check off): duplicate, dangling inner ref, cycle */
  auto craft = [](std::vector<std::vector<double>> pts, std::vector<PoolRef> src) {
    io::ByteWriter w;
    w.u32(1); /* format */
    w.u32(1); /* pools */
    w.str("test.lines");
    w.u32(1); /* traits version */
    w.u32(uint32_t(pts.size()));
    for (size_t i = 0; i < pts.size(); i++) {
      Polyline p;
      for (double x : pts[i]) {
        p.pts.append(math::double3(x, x, x));
      }
      p.source = src[i];
      p.write(w);
    }
    w.u64(0);
    w.u64(0);
    return std::vector<uint8_t>(w.data(), w.data() + w.size());
  };
  PoolReadLimits nohash;
  nohash.verify_hash = false;
  auto try_read = [&](const std::vector<uint8_t> &s) {
    Owner t;
    io::ByteReader r(s.data(), s.size());
    const char *err = nullptr;
    return t.store.read(r, nohash, &err);
  };
  test_assert(try_read(craft({{1}, {2}}, {kNoRef, kNoRef})));
  test_assert(!try_read(craft({{1}, {1}}, {kNoRef, kNoRef})));                     /* duplicate */
  test_assert(!try_read(craft({{1}, {2}}, {kNoRef, PoolRef{1, 5}})));              /* dangling */
  test_assert(!try_read(craft({{1}, {2}}, {PoolRef{1, 1}, PoolRef{1, 0}})));       /* cycle */
  test_assert(!try_read(craft({{1}}, {PoolRef{1, 0}})));                           /* self cycle */
  test_assert(!try_read(craft({{1}, {2}}, {kNoRef, PoolRef{0, 1}})));              /* malformed */
  test_assert(!try_read(craft({{1}, {2}}, {kNoRef, PoolRef{2, 0}})));              /* names pool: empty */
  /* the default (hash on) rejects a stream with a zero hash */
  {
    Owner t;
    const auto s = craft({{1}}, {kNoRef});
    io::ByteReader r(s.data(), s.size());
    test_assert(!t.store.read(r));
  }
  /* limits */
  {
    PoolReadLimits lim = nohash;
    lim.max_slots = 1;
    Owner t;
    const auto s = craft({{1}, {2}}, {kNoRef, kNoRef});
    io::ByteReader r(s.data(), s.size());
    test_assert(!t.store.read(r, lim));
    lim.max_slots = 100;
    lim.max_entry_bytes = 10;
    Owner t2;
    io::ByteReader r2(s.data(), s.size());
    test_assert(!t2.store.read(r2, lim));
  }
  /* a stale owner stream with a dangling group ref: audit rejects it */
  {
    Owner t;
    t.a.resize(1);
    t.curve().set(0, PoolRef{t.lines, 3});
    PoolStore s;
    s.add_pool<LinePool>("test.lines");
    io::ByteWriter w;
    w.u32(1);
    w.u32(0); /* no pools */
    hash::Hasher128 h(hash::DomainTag::Content);
    h.u32(0);
    const hash::Hash128 hh = h.finish();
    w.u64(hh.lo);
    w.u64(hh.hi);
    t.a.write(w);
    t.b.write(w);
    Owner f;
    io::ByteReader r(w.data(), w.size());
    test_assert(!Owner::read(r, f));
  }
}

/* Structural fuzz past both trailing hashes (B9): reads must fail cleanly or
 * succeed; never crash, leak or trip a sanitizer. */
static void test_structural_fuzz()
{
  Owner o;
  o.a.resize(12);
  o.b.resize(5);
  for (uint32_t i = 0; i < 12; i++) {
    o.curve().set(i, o.line({double(i % 4)}, i % 3 ? o.line({0.25 * (i % 2)}) : kNoRef));
  }
  for (uint32_t i = 0; i < 5; i++) {
    o.name().set(i, o.nm(i % 2 ? "a" : "bb"));
  }
  const auto bytes = owner_bytes(o);
  PoolReadLimits plim;
  plim.verify_hash = false;
  plim.max_slots = 1u << 12;
  ReadLimits glim;
  glim.verify_hash = false;
  glim.max_elements = 1u << 12;
  test::SplitMix64 rng(99);
  int accepted = 0;
  const int64_t n = test::cases(3000);
  for (int64_t it = 0; it < n; it++) {
    std::vector<uint8_t> m = bytes;
    const int edits = 1 + int(rng.below(3));
    for (int e = 0; e < edits; e++) {
      const size_t pos = size_t(rng.below(m.size()));
      switch (rng.below(4)) {
        case 0:
          m[pos] ^= uint8_t(1u << rng.below(8));
          break;
        case 1: /* small integer in a likely count/ref field */
          if (pos + 4 <= m.size()) {
            const uint32_t v = uint32_t(rng.below(6));
            memcpy(&m[pos], &v, 4);
          }
          break;
        case 2:
          m.resize(pos);
          break;
        default:
          m.insert(m.begin() + long(pos), uint8_t(rng.next()));
          break;
      }
      if (m.empty()) {
        break;
      }
    }
    Owner t;
    io::ByteReader r(m.data(), m.size());
    if (Owner::read(r, t, plim, glim)) {
      accepted++;
      test_assert(t.store.audit(t.roots(), true));
    }
  }
  printf("structural fuzz: %d / %lld mutated streams accepted (valid)\n", accepted, (long long)n);
}

static void test_import_rows_and_mix_from()
{
  Owner src, dst;
  src.a.resize(4);
  dst.a.resize(4);
  const PoolRef shared = src.line({9});
  src.curve().set(0, src.line({1}, shared));
  src.curve().set(1, src.line({2}, shared));
  dst.curve().set(3, dst.line({42}));
  /* cross-owner mix_from copies raw src refs into dst rows 0..1 ... */
  const uint32_t tg[] = {0, 1}, off[] = {0, 1, 2}, srcs[] = {0, 1};
  MixBatch b;
  b.targets = util::span<const uint32_t>(tg, 2);
  b.offsets = util::span<const uint32_t>(off, 3);
  b.sources = util::span<const uint32_t>(srcs, 2);
  test_assert(dst.a.mix_from(src.a, b));
  /* ... which import_rows rewrites into dst refs */
  PoolStore::ImportMemo memo;
  const uint32_t rows[] = {0, 1};
  test_assert(dst.store.import_rows(dst.curve(), util::span<const uint32_t>(rows, 2), src.store, memo));
  test_assert(dst.store.get<LinePool>(dst.curve().get(0)).pts[0][0] == 1);
  test_assert(dst.store.get<LinePool>(dst.curve().get(1)).pts[0][0] == 2);
  test_assert(dst.store.get<LinePool>(dst.curve().get(3)).pts[0][0] == 42);
  const PoolRef s0 = dst.store.get<LinePool>(dst.curve().get(0)).source;
  const PoolRef s1 = dst.store.get<LinePool>(dst.curve().get(1)).source;
  test_assert(s0 == s1 && dst.store.get<LinePool>(s0).pts[0][0] == 9);
  test_assert(dst.store.collect(dst.roots()) && dst.store.audit(dst.roots(), true));
  /* missing key */
  Owner nolines(kDefaultPageShift, false);
  PoolStore::ImportMemo m2;
  const char *err = nullptr;
  test_assert(nolines.store.import(src.store, shared, m2, &err).none() && err);
}

static void test_custom_ref_column_and_journal()
{
  Owner o;
  RefPairColumn *pc = o.a.ensure<RefPairColumn>("pairs");
  test_assert(pc != nullptr);
  o.a.resize(6);
  /* z is interned first, so canonicalize moves it (slot 0 -> 2): the journal
   * capture must be remapped */
  const PoolRef z = o.line({3});
  const PoolRef x = o.line({1}), y = o.line({2}), w = o.line({4});
  pc->set(2, RefPair{x, y});
  JournalRoot j;
  pc->set(3, RefPair{z, kNoRef});
  j.capture(*pc, 3); /* journal captured {z, none} before the overwrite */
  pc->set(3, RefPair{kNoRef, kNoRef});
  RefRoot *ex[] = {&j};
  test_assert(o.store.collect(o.roots(util::span<RefRoot *const>(ex, 1))));
  test_assert(o.store.live(x) && o.store.live(y) && o.store.live(z) && !o.store.live(w));
  test_assert(o.store.canonicalize(o.mroots(util::span<RefRoot *const>(ex, 1))));
  test_assert(o.store.audit(o.roots(util::span<RefRoot *const>(ex, 1)), true));
  /* undo through the journal after renumbering */
  pc->set_bytes(3, j.captures[0].data());
  test_assert(o.store.get<LinePool>(pc->get(3).a).pts[0][0] == 3);
  test_assert(o.store.get<LinePool>(pc->get(2).b).pts[0][0] == 2);
  test_assert(o.store.audit(o.roots(), true));
  /* Column<std::array<PoolRef, N>> is a compile error (B8); see pool_ref.h */
}

/* Weak roots: a source -> cache map pruned between mark and sweep (B6). */
class WeakCacheMap final : public RefRoot {
public:
  std::vector<std::pair<PoolRef, PoolRef>> entries; /* source -> cache, both weak */
  void visit_refs(RefVisitor &) const override {}
  void remap_refs(const RefRemap &m) override
  {
    for (auto &e : entries) {
      e = {m.map(e.first), m.map(e.second)};
    }
  }
  void prune(const MarkQuery &q) override
  {
    std::vector<std::pair<PoolRef, PoolRef>> keep;
    for (const auto &e : entries) {
      if (q.marked(e.first) && q.marked(e.second)) {
        keep.push_back(e);
      }
    }
    entries = keep;
  }
};

static void test_weak_roots_and_version()
{
  Owner o;
  o.a.resize(2);
  const PoolRef s = o.line({1});
  const PoolRef c = o.line({1, 1}, s);
  const PoolRef s2 = o.line({2});
  WeakCacheMap wm;
  wm.entries = {{s, c}, {s2, kNoRef}};
  o.curve().set(0, c);
  RefRoot *ex[] = {&wm};
  const uint64_t v0 = o.store.version();
  test_assert(o.store.collect(o.roots(util::span<RefRoot *const>(ex, 1))));
  test_assert(o.store.version() != v0); /* s2 was freed */
  test_assert(wm.entries.size() == 1 && wm.entries[0].first == s);
  const uint64_t v1 = o.store.version();
  test_assert(o.store.collect(o.roots(util::span<RefRoot *const>(ex, 1))));
  test_assert(o.store.version() == v1); /* nothing freed */
  o.line({1});                           /* hit */
  test_assert(o.store.version() == v1);
  o.line({77}); /* miss */
  test_assert(o.store.version() != v1);
}

/* Weak pairs (ephemerons): a cache entry that names its source lives exactly
 * while the source is reachable otherwise. */
class EphemeronMap final : public RefRoot {
public:
  std::vector<std::pair<PoolRef, PoolRef>> entries; /* key -> value */
  void visit_refs(RefVisitor &) const override {}
  void visit_weak_pairs(WeakPairVisitor &v) const override
  {
    for (const auto &e : entries) {
      v.visit(e.first, e.second);
    }
  }
  void remap_refs(const RefRemap &m) override
  {
    for (auto &e : entries) {
      e = {m.map(e.first), m.map(e.second)};
    }
  }
  void prune(const MarkQuery &q) override
  {
    std::vector<std::pair<PoolRef, PoolRef>> keep;
    for (const auto &e : entries) {
      if (q.marked(e.first) && q.marked(e.second)) {
        keep.push_back(e);
      }
    }
    entries = keep;
  }
};

static void test_weak_pairs()
{
  for (const bool canon : {false, true}) {
    Owner o;
    o.a.resize(2);
    const PoolRef s = o.line({1});
    const PoolRef c1 = o.line({1, 1}, s);  /* a cache of s, naming it */
    const PoolRef c2 = o.line({1, 1, 1}, c1); /* a cache of the cache */
    const PoolRef d = o.line({5});
    const PoolRef dc = o.line({5, 5}, d); /* cache of an unreachable key */
    EphemeronMap em;
    /* (c1, c2) first: reaching c2 takes a second round of the fixpoint */
    em.entries = {{c1, c2}, {s, c1}, {d, dc}};
    o.curve().set(0, s);
    RefRoot *ex[] = {&em};
    const auto run = [&] {
      if (canon) {
        test_assert(o.store.canonicalize(o.mroots(util::span<RefRoot *const>(ex, 1))));
      }
      else {
        test_assert(o.store.collect(o.roots(util::span<RefRoot *const>(ex, 1))));
      }
    };
    run();
    test_assert(em.entries.size() == 2);
    for (const auto &e : em.entries) {
      test_assert(o.store.live(e.first) && o.store.live(e.second));
    }
    test_assert(o.store.live(o.curve().get(0)) && o.store.live_count() == 3);
    test_assert(o.store.audit(o.roots(util::span<RefRoot *const>(ex, 1)), true));
    /* the key's last strong holder goes: the whole chain is freed and pruned */
    o.curve().set(0, kNoRef);
    run();
    test_assert(em.entries.empty() && o.store.live_count() == 0);
    test_assert(o.store.audit(o.roots(util::span<RefRoot *const>(ex, 1)), true));
  }
}

int main()
{
  const size_t baseline = alloc::getMemorySize();
  test_intern_and_get();
  test_collect_basics();
  test_collect_failures();
  test_inner_refs_and_chain();
  test_quarantine_detects_forgotten_root();
  test_snapshots_diverge();
  test_canonical_form();
  test_round_trip_and_read_failures();
  test_structural_fuzz();
  test_import_rows_and_mix_from();
  test_custom_ref_column_and_journal();
  test_weak_roots_and_version();
  test_weak_pairs();
  test_assert(alloc::getMemorySize() == baseline);
  return test_end();
}
