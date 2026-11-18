/* AttrGroup tests, the open-extension test (DEC-007: a custom ColumnBase
 * subclass registered from this file, no litestl changes), stale-reference
 * detection (death test), read/fuzz robustness and a threaded snapshot
 * stress test (run under TSan in the sanitizer configuration). */

#include "litestl/attr/bool_column.h"
#include "litestl/attr/group.h"
#include "litestl/attr/registry.h"

#include "splitmix64.h"
#include "test_harness.h"

#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/wait.h>
#include <unistd.h>
#define HAVE_FORK 1
#endif

test_init;

using namespace litestl;
using namespace litestl::attr;

/* ------------------------------------------------------------------------
 * Open extension: a column class unknown to litestl. Stores int32 pairs in a
 * std::vector (not paged at all), has its own type key, and a Custom mix
 * that sums the sources and writes a claimed sibling "guard" column.
 * ---------------------------------------------------------------------- */
struct Pair {
  int32_t a = 0, b = 0;
};

class TestPairColumn final : public ColumnBase {
public:
  using value_type = Pair;
  std::vector<Pair> v;
  Pair def;
  uint32_t guard_slot = 0xffffffffu;
  mutable uint64_t ver = next_version();

  TestPairColumn(const Pair &d = {}) : def(d)
  {
    default_policy_ = MixPolicy::Custom;
  }

  static std::string type_key()
  {
    return "test.pair";
  }
  static const ColumnType &static_type()
  {
    static const ColumnType *t = ColumnTypeRegistry::global().add_or_die<TestPairColumn>();
    return *t;
  }
  static util::UniquePtr<ColumnBase> create(const uint8_t *d, int)
  {
    Pair p;
    memcpy(&p, d, sizeof(p));
    return util::UniquePtr<ColumnBase>(util::make_unique<TestPairColumn>("TestPairColumn", p));
  }

  const ColumnType &type() const override
  {
    return static_type();
  }
  uint32_t size() const override
  {
    return uint32_t(v.size());
  }
  void resize(uint32_t n) override
  {
    v.resize(n, def);
    ver = next_version();
  }
  uint32_t page_shift() const override
  {
    return 31;
  }
  void swap(uint32_t a, uint32_t b) override
  {
    std::swap(v[a], v[b]);
    ver = next_version();
  }
  void permute(util::span<const uint32_t> m, uint32_t n) override
  {
    std::vector<Pair> nv(n, def);
    for (size_t o = 0; o < m.size(); o++) {
      if (m[o] != kDropped) {
        nv[m[o]] = v[o];
      }
    }
    v = nv;
    ver = next_version();
  }
  void set_default_range(uint32_t f, uint32_t n) override
  {
    for (uint32_t i = f; i < f + n; i++) {
      v[i] = def;
    }
  }
  void set_default_list(util::span<const uint32_t> e) override
  {
    for (uint32_t i : e) {
      v[i] = def;
    }
  }
  bool mix(const ValidatedMixBatch &b, MixPolicy p, const MixContext &ctx, const char **err) override
  {
    if (p == MixPolicy::Skip) {
      return true;
    }
    if (p != MixPolicy::Custom) {
      if (err) {
        *err = "pair: only Custom";
      }
      return false;
    }
    return mix_custom(b, nullptr, ctx);
  }
  bool mix_from(const ColumnBase &src, const ValidatedMixBatch &b, MixPolicy p, const MixContext &ctx, const char **err) override
  {
    if (&src.type() != &type()) {
      return false;
    }
    return p == MixPolicy::Custom ? mix_custom(b, &src, ctx) : (p == MixPolicy::Skip);
  }
  util::UniquePtr<ColumnBase> share() const override
  {
    auto c = util::make_unique<TestPairColumn>("TestPairColumn", def);
    c->v = v;
    c->flags_ = flags_;
    c->guard_slot = guard_slot;
    c->ver = ver;
    return util::UniquePtr<ColumnBase>(std::move(c));
  }
  util::UniquePtr<ColumnBase> new_like(uint32_t n) const override
  {
    auto c = util::make_unique<TestPairColumn>("TestPairColumn", def);
    c->v.resize(n, def);
    c->flags_ = flags_;
    c->guard_slot = guard_slot;
    return util::UniquePtr<ColumnBase>(std::move(c));
  }
  uint32_t elem_size() const override
  {
    return sizeof(Pair);
  }
  void get_bytes(uint32_t i, uint8_t *o) const override
  {
    memcpy(o, &v[i], sizeof(Pair));
  }
  void set_bytes(uint32_t i, const uint8_t *in) override
  {
    memcpy(&v[i], in, sizeof(Pair));
  }
  void default_bytes(uint8_t *o) const override
  {
    memcpy(o, &def, sizeof(Pair));
  }
  uint32_t page_count() const override
  {
    return v.empty() ? 0 : 1;
  }
  PageState page_state(uint32_t) const override
  {
    return PageState::Dense;
  }
  bool same_page_as(const ColumnBase &, uint32_t) const override
  {
    return false;
  }
  void hash_into(hash::Hasher128 &h) const override
  {
    h.str(type().key);
    h.u32(uint32_t(flags_));
    h.u32(size());
    h.raw_bytes(&def, sizeof(def));
    h.raw_bytes(v.data(), v.size() * sizeof(Pair));
  }
  void write(io::ByteWriter &w) const override
  {
    w.u32(uint32_t(flags_));
    w.u32(size());
    w.put(v.data(), v.size() * sizeof(Pair));
  }
  bool read_data(io::ByteReader &r, const ReadLimits &lim) override
  {
    flags_ = ColumnFlags(r.u32());
    const uint32_t n = r.u32();
    if (r.failed() || n > lim.max_elements || uint64_t(n) * sizeof(Pair) > r.remaining()) {
      return false;
    }
    v.resize(n);
    return r.get(v.data(), n * sizeof(Pair));
  }
  uint64_t version() const override
  {
    return ver;
  }
  util::span<const uint32_t> claimed_slots() const override
  {
    return guard_slot == 0xffffffffu ? util::span<const uint32_t>() :
                                       util::span<const uint32_t>(&guard_slot, 1);
  }

protected:
  bool mix_custom(const ValidatedMixBatch &vb, const ColumnBase *src, const MixContext &ctx) override
  {
    const TestPairColumn &from = src ? static_cast<const TestPairColumn &>(*src) : *this;
    const std::vector<Pair> pre = from.v; /* simultaneous: gather first */
    const MixBatch &b = vb.batch();
    for (size_t k = 0; k < b.targets.size(); k++) {
      Pair s;
      for (uint32_t q = b.offsets[k]; q < b.offsets[k + 1]; q++) {
        s.a += pre[b.sources[q]].a;
        s.b += pre[b.sources[q]].b;
      }
      v[b.targets[k]] = s;
    }
    /* the claimed guard column: read its pre-call state from ctx.pre, write it here */
    if (guard_slot != 0xffffffffu && ctx.pre) {
      const auto &pre_guard = ctx.pre->builtin<Column<int32_t>>(guard_slot);
      for (size_t k = 0; k < b.targets.size(); k++) {
        last_guard_seen = pre_guard.get(b.targets[k]);
      }
    }
    ver = next_version();
    return true;
  }
  void on_flags_changed() override
  {
    ver = next_version();
  }

public:
  int32_t last_guard_seen = -1;
};

/* ------------------------------------------------------------------------ */

static void test_builtins_and_lookup()
{
  AttrGroup g(4);
  auto *pos = g.add_builtin<Column<math::double3>>("pos");
  auto *next = g.add_builtin<Column<uint32_t>>(".next", 0xffffffffu, ColumnFlags::Topo);
  test_assert(pos && next && g.builtin_count() == 2);
  g.resize(40);
  test_assert(g.builtin<Column<uint32_t>>(1)[39] == 0xffffffffu);

  auto *w = g.ensure<Column<float>>("weight", 1.0f);
  test_assert(w && g.ensure<Column<float>>("weight", 1.0f) == w);
  test_assert(!g.ensure<Column<float>>("weight", 2.0f));   /* other default */
  test_assert(!g.ensure<Column<double>>("weight", 1.0));   /* other type */
  test_assert(!g.find<Column<double>>("weight") && g.find<Column<float>>("weight") == w);
  test_assert(!g.add_builtin<Column<float>>("late"));       /* after a dynamic column */
  test_assert(!g.remove("pos") && g.remove("weight") && !g.find_any("weight"));

  g.builtin<Column<math::double3>>(0).set(5, math::double3(1, 2, 3));
  /* copy and move keep slots valid */
  AttrGroup c = g;
  AttrGroup m = std::move(c);
  test_assert(m.builtin<Column<math::double3>>(0)[5][1] == 2.0);
  /* snapshot independence */
  g.builtin<Column<math::double3>>(0).set(5, math::double3(9, 9, 9));
  test_assert(m.builtin<Column<math::double3>>(0)[5][0] == 1.0);
}

static void test_permute_and_faceside()
{
  const char *err = nullptr;
  AttrGroup faces(2), sides(2);
  faces.add_builtin<Column<int32_t>>("id");
  sides.add_builtin<Column<int32_t>>("mat");
  const uint32_t nf = 7;
  faces.resize(nf);
  sides.resize(2 * nf);
  for (uint32_t f = 0; f < nf; f++) {
    faces.builtin<Column<int32_t>>(0).set(f, int32_t(f));
    sides.builtin<Column<int32_t>>(0).set(2 * f, int32_t(100 + f));
    sides.builtin<Column<int32_t>>(0).set(2 * f + 1, int32_t(200 + f));
  }
  std::vector<uint32_t> pi = {3, 0, 6, ColumnBase::kDropped, 1, 2, 4}; /* face 3 dropped */
  std::vector<uint32_t> bad = {0, 0, 1, 2, 3, 4, 5};
  test_assert(!faces.permute(util::span<const uint32_t>(bad.data(), bad.size()), 6, &err));
  test_assert(faces.permute(util::span<const uint32_t>(pi.data(), pi.size()), 7, &err));
  /* induced FaceSide permutation 2*pi(f)+s */
  std::vector<uint32_t> spi(2 * nf);
  for (uint32_t f = 0; f < nf; f++) {
    for (uint32_t s = 0; s < 2; s++) {
      spi[2 * f + s] = pi[f] == ColumnBase::kDropped ? ColumnBase::kDropped : 2 * pi[f] + s;
    }
  }
  test_assert(sides.permute(util::span<const uint32_t>(spi.data(), spi.size()), 2 * nf, &err));
  for (uint32_t f = 0; f < nf; f++) {
    if (pi[f] == ColumnBase::kDropped) {
      continue;
    }
    test_assert(faces.builtin<Column<int32_t>>(0)[pi[f]] == int32_t(f));
    test_assert(sides.builtin<Column<int32_t>>(0)[2 * pi[f]] == int32_t(100 + f));
    test_assert(sides.builtin<Column<int32_t>>(0)[2 * pi[f] + 1] == int32_t(200 + f));
  }
}

static void test_mix()
{
  AttrGroup g(2);
  g.add_builtin<Column<uint32_t>>(".link", 7u, ColumnFlags::Topo);
  auto *t = g.ensure<Column<float>>("temp", 0.0f);
  auto *id = g.ensure<Column<int32_t>>("id", -1);
  g.resize(10);
  for (uint32_t i = 0; i < 10; i++) {
    t->set(i, float(i));
    id->set(i, int32_t(i));
    g.builtin<Column<uint32_t>>(0).set(i, i);
  }
  /* side swap: 2 <- 3, 3 <- 2 (simultaneous) */
  std::vector<uint32_t> targets = {2, 3}, offsets = {0, 1, 2}, sources = {3, 2};
  MixBatch b{targets, offsets, sources, {}};
  const char *err = nullptr;
  AttrGroup::PolicyFn pol = [](std::string_view name, uint32_t, const ColumnBase &) {
    return name == "temp" ? MixPolicy::CopyFirst : MixPolicy::CopyFirst;
  };
  test_assert(g.mix(b, &pol, nullptr, &err));
  test_assert((*t)[2] == 3.0f && (*t)[3] == 2.0f && (*id)[2] == 3 && (*id)[3] == 2);
  test_assert(g.builtin<Column<uint32_t>>(0)[2] == 2u); /* Topo untouched */
  /* interpolation by name; duplicate targets rejected */
  std::vector<uint32_t> t2 = {5}, o2 = {0, 2}, s2 = {0, 9};
  std::vector<double> w2 = {0.25, 0.75};
  AttrGroup::PolicyFn pol2 = [](std::string_view name, uint32_t, const ColumnBase &) {
    return name == "temp" ? MixPolicy::Interpolate : MixPolicy::CopyFirst;
  };
  test_assert(g.mix(MixBatch{t2, o2, s2, w2}, &pol2, nullptr, &err));
  test_assert((*t)[5] == 0.25f * 0.0f + 0.75f * 9.0f && (*id)[5] == 0);
  std::vector<uint32_t> dup = {1, 1}, od = {0, 0, 0};
  test_assert(!g.mix(MixBatch{dup, od, {}, {}}, nullptr, nullptr, &err));
  std::vector<uint32_t> o_bad = {1, 1};
  test_assert(!g.mix(MixBatch{t2, o_bad, s2, {}}, nullptr, nullptr, &err));

  /* mix_from creates missing columns */
  AttrGroup dst(2);
  dst.resize(4);
  std::vector<uint32_t> t3 = {0, 1}, o3 = {0, 1, 2}, s3 = {9, 8};
  test_assert(dst.mix_from(g, MixBatch{t3, o3, s3, {}}, nullptr, nullptr, &err));
  test_assert(dst.find<Column<float>>("temp") && (*dst.find<Column<float>>("temp"))[0] == 9.0f);
  test_assert(!dst.find_any(".link")); /* Topo columns are not transferred */
}

static void test_open_extension_and_custom()
{
  AttrGroup g(3);
  auto *guard = g.add_builtin<Column<int32_t>>("pair_guard", 0);
  (void)guard;
  g.resize(6);
  auto *pairs = static_cast<TestPairColumn *>(g.find_any("pairs"));
  test_assert(!pairs);
  /* ensure works for the custom class through the same templated API */
  pairs = g.ensure<TestPairColumn>("pairs", Pair{});
  test_assert(pairs && pairs->size() == 6);
  pairs->guard_slot = 0;
  for (uint32_t i = 0; i < 6; i++) {
    pairs->v[i] = Pair{int32_t(i), int32_t(10 * i)};
    g.builtin<Column<int32_t>>(0).set(i, int32_t(i + 100));
  }
  std::vector<uint32_t> targets = {0, 1}, offsets = {0, 2, 3}, sources = {1, 2, 0};
  const char *err = nullptr;
  test_assert(g.mix(MixBatch{targets, offsets, sources, {}}, nullptr, nullptr, &err));
  test_assert(pairs->v[0].a == 3 && pairs->v[0].b == 30 && pairs->v[1].a == 0);
  test_assert(pairs->last_guard_seen == 101); /* read from the pre-call snapshot */
  /* claimed guard column was skipped by the generic loop */
  test_assert(g.builtin<Column<int32_t>>(0)[0] == 100);

  /* snapshot, hash, write, read: no litestl change needed */
  AttrGroup snap = g;
  pairs->v[2].a = 999;
  test_assert(static_cast<TestPairColumn *>(snap.find_any("pairs"))->v[2].a == 2);
  io::ByteWriter w;
  snap.write(w);
  AttrGroup back(3);
  back.add_builtin<Column<int32_t>>("pair_guard", 0);
  io::ByteReader r(w.data(), w.size());
  test_assert(back.read(r, {}, &err) && r.at_end());
  test_assert(back.content_hash() == snap.content_hash());
}

static void test_canonical_forms()
{
  auto build = [](bool reverse) {
    AttrGroup g(2);
    g.add_builtin<Column<int32_t>>("b0");
    g.resize(20);
    if (reverse) {
      g.ensure<Column<float>>("zeta")->set(3, 1.0f);
      g.ensure<Column<double>>("alpha")->set(4, 2.0);
      g.ensure<Column<float>>("temp", 0.0f, ColumnFlags::Temp)->set(1, 5.0f);
    }
    else {
      g.ensure<Column<double>>("alpha")->set(4, 2.0);
      g.ensure<Column<int32_t>>("gone");
      g.ensure<Column<float>>("zeta")->set(3, 1.0f);
      g.remove("gone");
    }
    return g;
  };
  AttrGroup a = build(false), b = build(true);
  test_assert(a.content_hash() == b.content_hash()); /* order + Temp independent */
  io::ByteWriter wa, wb;
  a.write(wa);
  b.write(wb);
  test_assert(wa.size() == wb.size() && memcmp(wa.data(), wb.data(), wa.size()) == 0);
  a.find<Column<float>>("zeta")->set(3, -0.0f);
  AttrGroup c = build(false);
  c.find<Column<float>>("zeta")->set(3, 0.0f);
  test_assert(a.content_hash() != c.content_hash()); /* -0 vs +0 */
}

static void test_read_failures_and_fuzz()
{
  AttrGroup g(2);
  g.add_builtin<Column<int32_t>>("b0", 3);
  g.add_builtin<Column<float>>("tmp", 0.0f, ColumnFlags::Temp);
  g.resize(50);
  g.ensure<Column<uint64_t>>("u")->set(7, 77);
  g.builtin<Column<int32_t>>(0).fill(10, 30, 9);
  io::ByteWriter w;
  g.write(w);
  const char *err = nullptr;

  /* layout mismatch: owner with a different builtin default */
  {
    AttrGroup o(2);
    o.add_builtin<Column<int32_t>>("b0", 4);
    o.add_builtin<Column<float>>("tmp", 0.0f, ColumnFlags::Temp);
    o.ensure<Column<int32_t>>("keep");
    io::ByteReader r(w.data(), w.size());
    test_assert(!o.read(r, {}, &err) && o.find_any("keep")); /* unchanged on failure */
  }
  /* limits */
  {
    AttrGroup o(2);
    o.add_builtin<Column<int32_t>>("b0", 3);
    o.add_builtin<Column<float>>("tmp", 0.0f, ColumnFlags::Temp);
    io::ByteReader r(w.data(), w.size());
    test_assert(!o.read(r, ReadLimits{10, 16}, &err));
  }
  /* fuzz: truncations and byte flips must never crash, leak or misbehave */
  test::SplitMix64 rng(42);
  int accepted_corrupt = 0;
  for (int it = 0; it < 4000; it++) {
    std::vector<uint8_t> buf(w.data(), w.data() + w.size());
    if (rng.coin()) {
      buf.resize(rng.below(buf.size()));
    }
    else {
      for (int k = 0; k < 1 + int(rng.below(3)); k++) {
        buf[rng.below(buf.size())] ^= uint8_t(1u << rng.below(8));
      }
    }
    AttrGroup o(2);
    o.add_builtin<Column<int32_t>>("b0", 3);
    o.add_builtin<Column<float>>("tmp", 0.0f, ColumnFlags::Temp);
    io::ByteReader r(buf.data(), buf.size());
    if (o.read(r, ReadLimits{1u << 20, 64}, &err)) {
      accepted_corrupt++; /* allowed only if the content hash still matched */
    }
  }
  test_assert(accepted_corrupt < 40); /* the trailing hash rejects nearly all corruption */
}

#ifdef HAVE_FORK
/* A1: a write through a reference held across share() is detected. */
static void test_stale_reference_detected()
{
#if LITESTL_ATTR_CHECKS
  const pid_t pid = fork();
  if (pid == 0) {
    fclose(stderr); /* silence the expected diagnostic */
    Column<float> c(0.0f, 4);
    c.resize(32);
    float &r = c.write(5);
    auto snap = c.share();
    r = 42.0f; /* writes into the now-shared page: forbidden */
    snap.reset(); /* release while shared -> checksum mismatch -> abort */
    _exit(0);
  }
  int status = 0;
  waitpid(pid, &status, 0);
  test_assert(WIFSIGNALED(status)); /* the child must abort */
#endif
}
#endif

/* A21: one writer, readers receiving snapshots through a mutex-protected slot. */
static void test_threaded_snapshots()
{
  AttrGroup g(4);
  g.add_builtin<Column<double>>("x", 0.0);
  g.resize(500);
  std::mutex slot_mutex;
  util::UniquePtr<AttrGroup> slot;
  hash::Hash128 slot_hash;
  std::atomic<bool> done{false};
  std::atomic<int> bad{0};
  std::vector<std::thread> readers;
  for (int t = 0; t < 4; t++) {
    readers.emplace_back([&] {
      while (!done.load()) {
        util::UniquePtr<AttrGroup> mine;
        hash::Hash128 expect;
        {
          std::lock_guard lk(slot_mutex);
          if (slot) {
            mine = util::make_unique<AttrGroup>("reader snapshot", *slot);
            expect = slot_hash;
          }
        }
        if (mine) {
          AttrGroup copy = *mine; /* copy and destroy owned snapshots */
          if (copy.content_hash() != expect || mine->content_hash() != expect) {
            bad.fetch_add(1);
          }
        }
      }
    });
  }
  test::SplitMix64 rng(3);
  for (int it = 0; it < test::cases(300); it++) {
    for (int k = 0; k < 20; k++) {
      g.builtin<Column<double>>(0).set(uint32_t(rng.below(500)), rng.unit());
    }
    auto snap = util::make_unique<AttrGroup>("writer snapshot", g);
    const hash::Hash128 h = snap->content_hash();
    std::lock_guard lk(slot_mutex);
    slot = std::move(snap);
    slot_hash = h;
  }
  done.store(true);
  for (auto &th : readers) {
    th.join();
  }
  slot.reset();
  test_assert(bad.load() == 0);
}

static void test_filtered_hash_and_version()
{
  AttrGroup g(4);
  g.add_builtin<Column<math::double3>>("pos");
  g.add_builtin<Column<uint32_t>>(".next", 0xffffffffu, ColumnFlags::Topo);
  g.ensure<Column<float>>("cache", 0.0f, ColumnFlags::Derived);
  g.resize(30);
  g.builtin<Column<math::double3>>(0).set(3, math::double3(1, 2, 3));
  g.builtin<Column<uint32_t>>(1).set(4, 7u);

  /* keep-all filter == plain hash */
  hash::Hasher128 a(hash::DomainTag::Test), b(hash::DomainTag::Test);
  g.hash_into(a);
  g.hash_into(b, [](std::string_view, const ColumnBase &) { return true; });
  test_assert(a.finish() == b.finish());

  /* a Topo-only hash ignores geometry edits and sees topology edits */
  auto topo_only = [](std::string_view, const ColumnBase &c) { return has_flag(c.flags(), ColumnFlags::Topo); };
  auto topo_hash = [&] {
    hash::Hasher128 h(hash::DomainTag::Test);
    g.hash_into(h, topo_only);
    return h.finish();
  };
  const hash::Hash128 h0 = topo_hash();
  test_assert(h0 != a.finish());
  g.builtin<Column<math::double3>>(0).set(3, math::double3(5, 5, 5));
  test_assert(topo_hash() == h0);
  g.builtin<Column<uint32_t>>(1).set(5, 9u);
  test_assert(topo_hash() != h0);

  /* version: changes after every kind of mutation, never repeats */
  util::Vector<uint64_t> seen;
  auto bump = [&](const char *what) {
    const uint64_t v = g.version();
    for (uint64_t s : seen) {
      if (s == v) {
        fprintf(stderr, "version did not change after %s\n", what);
        test_assert(false);
      }
    }
    seen.append(v);
  };
  bump("start");
  test_assert(g.version() == seen[0]); /* stable without mutation */
  g.builtin<Column<math::double3>>(0).set(1, math::double3(1, 1, 1));
  bump("set");
  g.resize(31);
  bump("resize");
  g.swap(1, 2);
  bump("swap");
  g.ensure<Column<int32_t>>("extra");
  bump("ensure");
  g.remove("extra");
  bump("remove");
  const uint32_t drop[] = {0};
  g.set_default_list(drop);
  bump("set_default_list");
  util::Vector<uint32_t> perm;
  for (uint32_t i = 0; i < 31; i++) {
    perm.append(30 - i);
  }
  test_assert(g.permute(util::span<const uint32_t>(perm.data(), perm.size()), 31));
  bump("permute");
  AttrGroup snap = g;
  test_assert(snap.version() == g.version()); /* a snapshot has the same stamp */
  snap.builtin<Column<uint32_t>>(1).set(0, 1u);
  g.builtin<Column<uint32_t>>(1).set(0, 2u);
  test_assert(snap.version() != g.version()); /* diverged copies never collide */
}

int main()
{
  const size_t baseline = alloc::getMemorySize();
  test_builtins_and_lookup();
  test_permute_and_faceside();
  test_mix();
  test_open_extension_and_custom();
  test_canonical_forms();
  test_read_failures_and_fuzz();
#ifdef HAVE_FORK
  test_stale_reference_detected();
#endif
  test_threaded_snapshots();
  test_filtered_hash_and_version();
  test_assert(alloc::getMemorySize() == baseline);
  return test_end();
}
