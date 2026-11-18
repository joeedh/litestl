/* Model-based test of value pools (P1b W5): random operations over several
 * diverging test owners, checked after every step against a reference model
 * that stores each row's *content* (not slots). Covers interning, every
 * structural column operation (through the group and directly), dynamic and
 * Temp ref columns, snapshots, collect, canonicalize, write/read round trips
 * and cross-owner mix_from + import_rows. Slot page shifts 2 and 10, column
 * page shifts 2 and 12. Honours LITESTL_TEST_SCALE(_DIV). */

#include "pool_test_types.h"

#include "splitmix64.h"
#include "test_harness.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

test_init;

using namespace pooltest;

namespace {

/* Content specs: base lines B(k) = line({k}); caches C(k, j) = line({k, j}, src B(j)). */
struct Spec {
  int k = -1; /* -1: none */
  int j = -1; /* -1: base line */
};

std::string render_spec(const Spec &s)
{
  if (s.k < 0) {
    return "-";
  }
  std::string r = "L(" + std::to_string(s.k);
  if (s.j >= 0) {
    r += "," + std::to_string(s.j) + "<L(" + std::to_string(s.j) + ")>";
  }
  return r + ")";
}

template <int S> std::string render_line(const PoolStore &st, PoolRef r)
{
  if (r.none()) {
    return "-";
  }
  const Polyline *p = st.try_get<LinePoolT<S>>(r);
  if (!p) {
    return "?dead";
  }
  std::string out = "L(";
  for (int i = 0; i < p->pts.size(); i++) {
    out += (i ? "," : "") + std::to_string(int(p->pts[i][0]));
  }
  if (!p->source.none()) {
    out += "<" + render_line<S>(st, p->source) + ">";
  }
  return out + ")";
}

std::string render_name(const PoolStore &st, PoolRef r)
{
  if (r.none()) {
    return "-";
  }
  const util::string *s = st.try_get<NamePool>(r);
  return s ? std::string(std::string_view(*s)) : "?dead";
}

template <int S> struct World {
  using O = OwnerT<S>;
  struct Slot {
    std::unique_ptr<O> o;
    std::vector<std::string> curve, name, dyn;
    bool has_dyn = false, dyn_temp = false;
  };
  int col_shift;
  test::SplitMix64 rng;
  Slot slots[4];
  test::FailureLog log;
  int64_t steps = 0;

  World(int cs, uint64_t seed) : col_shift(cs), rng(seed), log("pool model") {}

  PoolRef make_line(O &o, const Spec &s)
  {
    if (s.k < 0) {
      return kNoRef;
    }
    const PoolRef src = s.j >= 0 ? o.line({double(s.j)}) : kNoRef;
    return s.j >= 0 ? o.line({double(s.k), double(s.j)}, src) : o.line({double(s.k)});
  }
  Spec rand_spec()
  {
    Spec s;
    if (rng.below(6) == 0) {
      return s;
    }
    s.k = int(rng.below(12));
    s.j = rng.coin() ? int(rng.below(5)) : -1;
    return s;
  }

  Column<PoolRef> *dyn_col(O &o)
  {
    return o.a.template find<Column<PoolRef>>("dyn");
  }

  void ensure_slot(int i)
  {
    if (!slots[i].o) {
      slots[i].o = std::make_unique<O>(col_shift);
      slots[i].curve.clear();
      slots[i].name.clear();
      slots[i].dyn.clear();
      slots[i].has_dyn = false;
    }
  }

  void check(int i, const char *op)
  {
    Slot &sl = slots[i];
    if (!sl.o) {
      return;
    }
    O &o = *sl.o;
    bool ok = o.a.size() == sl.curve.size() && o.b.size() == sl.name.size();
    for (uint32_t r = 0; ok && r < o.a.size(); r++) {
      ok = render_line<S>(o.store, o.curve().get(r)) == sl.curve[r];
      if (ok && sl.has_dyn) {
        ok = dyn_col(o) && render_line<S>(o.store, dyn_col(o)->get(r)) == sl.dyn[r];
      }
    }
    for (uint32_t r = 0; ok && r < o.b.size(); r++) {
      ok = render_name(o.store, o.name().get(r)) == sl.name[r];
    }
    if (!ok) {
      log.fail([&] { fprintf(stderr, "slot %d after %s (step %lld)", i, op, (long long)steps); });
    }
  }

  void resize_model(Slot &sl, uint32_t n)
  {
    sl.curve.resize(n, "-");
    if (sl.has_dyn) {
      sl.dyn.resize(n, "-");
    }
  }

  void step()
  {
    steps++;
    const int i = int(rng.below(4));
    ensure_slot(i);
    Slot &sl = slots[i];
    O &o = *sl.o;
    const char *op = "?";
    switch (rng.below(16)) {
      case 0:
      case 1: { /* assign a curve row */
        op = "assign";
        if (o.a.size() == 0) {
          o.a.resize(8);
          resize_model(sl, 8);
        }
        const uint32_t r = uint32_t(rng.below(o.a.size()));
        const Spec s = rand_spec();
        o.curve().set(r, make_line(o, s));
        sl.curve[r] = render_spec(s);
        break;
      }
      case 2: { /* names */
        op = "name";
        if (o.b.size() == 0) {
          o.b.resize(4);
          sl.name.resize(4, "-");
        }
        const uint32_t r = uint32_t(rng.below(o.b.size()));
        const int v = int(rng.below(5));
        if (v == 0) {
          o.name().set(r, kNoRef);
          sl.name[r] = "-";
        }
        else {
          const std::string nm = "n" + std::to_string(v);
          o.name().set(r, o.nm(nm.c_str()));
          sl.name[r] = nm;
        }
        break;
      }
      case 3: { /* fill (uniform pages) */
        op = "fill";
        if (o.a.size() == 0) {
          break;
        }
        const uint32_t f = uint32_t(rng.below(o.a.size()));
        const uint32_t n = uint32_t(rng.below(o.a.size() - f + 1));
        const Spec s = rand_spec();
        o.curve().fill(f, n, make_line(o, s));
        for (uint32_t r = f; r < f + n; r++) {
          sl.curve[r] = render_spec(s);
        }
        break;
      }
      case 4: { /* resize through the group */
        op = "resize";
        const uint32_t n = uint32_t(rng.below(40));
        o.a.resize(n);
        resize_model(sl, n);
        break;
      }
      case 5: { /* swap through the group */
        op = "swap";
        if (o.a.size() < 2) {
          break;
        }
        const uint32_t x = uint32_t(rng.below(o.a.size())), y = uint32_t(rng.below(o.a.size()));
        o.a.swap(x, y);
        std::swap(sl.curve[x], sl.curve[y]);
        if (sl.has_dyn) {
          std::swap(sl.dyn[x], sl.dyn[y]);
        }
        break;
      }
      case 6: { /* permute with drops */
        op = "permute";
        const uint32_t n = o.a.size();
        std::vector<uint32_t> perm(n);
        for (uint32_t k = 0; k < n; k++) {
          perm[k] = k;
        }
        for (uint32_t k = n; k > 1; k--) {
          std::swap(perm[k - 1], perm[rng.below(k)]);
        }
        const uint32_t nn = n ? uint32_t(rng.below(n + 4)) : 0;
        std::vector<uint32_t> o2n(n);
        for (uint32_t k = 0; k < n; k++) {
          o2n[k] = perm[k] < nn ? perm[k] : ColumnBase::kDropped;
        }
        test_assert(o.a.permute(util::span<const uint32_t>(o2n.data(), n), nn));
        std::vector<std::string> nc(nn, "-"), nd(nn, "-");
        for (uint32_t k = 0; k < n; k++) {
          if (o2n[k] != ColumnBase::kDropped) {
            nc[o2n[k]] = sl.curve[k];
            if (sl.has_dyn) {
              nd[o2n[k]] = sl.dyn[k];
            }
          }
        }
        sl.curve = nc;
        if (sl.has_dyn) {
          sl.dyn = nd;
        }
        break;
      }
      case 7: { /* set_default_list (direct, on the column) */
        op = "set_default_list";
        std::vector<uint32_t> rows;
        for (uint32_t r = 0; r < o.a.size(); r++) {
          if (rng.below(4) == 0) {
            rows.push_back(r);
            sl.curve[r] = "-";
          }
        }
        o.curve().set_default_list(util::span<const uint32_t>(rows.data(), rows.size()));
        break;
      }
      case 8: { /* simultaneous CopyFirst mix through the group */
        op = "mix";
        const uint32_t n = o.a.size();
        if (n == 0) {
          break;
        }
        std::vector<uint32_t> tg, off{0}, src;
        std::vector<bool> used(n, false);
        for (int k = 0; k < 4; k++) {
          const uint32_t t = uint32_t(rng.below(n));
          if (used[t]) {
            continue;
          }
          used[t] = true;
          tg.push_back(t);
          const int ns = int(rng.below(3));
          for (int q = 0; q < ns; q++) {
            src.push_back(uint32_t(rng.below(n)));
          }
          off.push_back(uint32_t(src.size()));
        }
        MixBatch b;
        b.targets = util::span<const uint32_t>(tg.data(), tg.size());
        b.offsets = util::span<const uint32_t>(off.data(), off.size());
        b.sources = util::span<const uint32_t>(src.data(), src.size());
        test_assert(o.a.mix(b));
        const std::vector<std::string> pre = sl.curve, pred = sl.dyn;
        for (size_t k = 0; k < tg.size(); k++) {
          const bool empty = off[k] == off[k + 1];
          sl.curve[tg[k]] = empty ? "-" : pre[src[off[k]]];
          if (sl.has_dyn) {
            sl.dyn[tg[k]] = empty ? "-" : pred[src[off[k]]];
          }
        }
        break;
      }
      case 9: { /* snapshot into another slot / destroy / assign */
        const int j = int(rng.below(4));
        if (j == i) {
          op = "destroy";
          if (rng.below(3) == 0) {
            sl.o.reset();
          }
          break;
        }
        op = "snapshot";
        Slot &dl = slots[j];
        if (dl.o && rng.coin()) {
          *dl.o = o; /* assignment over an existing owner */
        }
        else {
          dl.o = std::make_unique<O>(o);
        }
        dl.curve = sl.curve;
        dl.name = sl.name;
        dl.dyn = sl.dyn;
        dl.has_dyn = sl.has_dyn;
        dl.dyn_temp = sl.dyn_temp;
        check(j, op);
        break;
      }
      case 10:
      case 11: {
        op = "collect";
        test_assert(o.store.collect(o.roots()));
        if (rng.below(4) == 0) {
          test_assert(o.store.audit(o.roots(), true));
        }
        break;
      }
      case 12: {
        op = "canonicalize";
        test_assert(o.store.canonicalize(o.mroots()));
        test_assert(o.store.canonical(o.roots()));
        break;
      }
      case 13: { /* write/read round trip into a fresh owner */
        op = "round trip";
        io::ByteWriter w;
        test_assert(o.write(w));
        auto f = std::make_unique<O>(col_shift);
        io::ByteReader r(w.data(), w.size());
        test_assert(O::read(r, *f));
        /* dynamic Temp columns are not serialized at all */
        if (sl.has_dyn && sl.dyn_temp) {
          sl.has_dyn = false;
          sl.dyn.clear();
        }
        sl.o = std::move(f);
        break;
      }
      case 14: { /* cross-owner mix_from + import_rows */
        op = "import";
        const int j = int(rng.below(4));
        if (j == i || !slots[j].o || slots[j].o->a.size() == 0 || o.a.size() == 0) {
          break;
        }
        O &src = *slots[j].o;
        if (sl.has_dyn != slots[j].has_dyn || (sl.has_dyn && sl.dyn_temp != slots[j].dyn_temp)) {
          break; /* mix_from would create or mismatch the dynamic column */
        }
        const uint32_t t = uint32_t(rng.below(o.a.size()));
        const uint32_t s = uint32_t(rng.below(src.a.size()));
        const uint32_t tg[] = {t}, off[] = {0, 1}, sr[] = {s};
        MixBatch b;
        b.targets = util::span<const uint32_t>(tg, 1);
        b.offsets = util::span<const uint32_t>(off, 2);
        b.sources = util::span<const uint32_t>(sr, 1);
        test_assert(o.a.mix_from(src.a, b));
        PoolStore::ImportMemo memo;
        test_assert(o.store.import_rows(o.curve(), util::span<const uint32_t>(tg, 1), src.store, memo));
        sl.curve[t] = slots[j].curve[s];
        if (sl.has_dyn) {
          test_assert(o.store.import_rows(*dyn_col(o), util::span<const uint32_t>(tg, 1), src.store, memo));
          sl.dyn[t] = slots[j].dyn[s];
        }
        break;
      }
      default: { /* dynamic ref column: add (maybe Temp), write, or remove */
        op = "dyn";
        if (!sl.has_dyn) {
          sl.dyn_temp = rng.coin();
          test_assert(o.a.template ensure<Column<PoolRef>>(
                          "dyn", kNoRef, sl.dyn_temp ? ColumnFlags::Temp : ColumnFlags::None) != nullptr);
          sl.has_dyn = true;
          sl.dyn.assign(o.a.size(), "-");
        }
        else if (rng.below(4) == 0) {
          test_assert(o.a.remove("dyn"));
          sl.has_dyn = false;
          sl.dyn.clear();
        }
        else if (o.a.size()) {
          const uint32_t r = uint32_t(rng.below(o.a.size()));
          const Spec s = rand_spec();
          dyn_col(o)->set(r, make_line(o, s));
          sl.dyn[r] = render_spec(s);
        }
        break;
      }
    }
    if (sl.o) {
      check(i, op);
      if (steps % 16 == 0) {
        util::Vector<util::string> probs;
        if (!sl.o->store.audit(sl.o->roots(), false, &probs)) {
          log.fail([&] {
            fprintf(stderr, "audit after %s: %s", op, probs.size() ? probs[0].c_str() : "?");
          });
        }
      }
    }
  }
};

template <int S> void run(int col_shift, uint64_t seed, int64_t n)
{
  World<S> w(col_shift, seed);
  for (int64_t k = 0; k < n; k++) {
    w.step();
  }
  for (int i = 0; i < 4; i++) {
    w.check(i, "final");
  }
  test_assert(w.log.failures() == 0);
  printf("pool model: slot shift %d, column shift %d: %lld steps, %lld failures\n", S, col_shift,
         (long long)n, (long long)w.log.failures());
}

} // namespace

int main()
{
  const size_t baseline = alloc::getMemorySize();
  const int64_t n = test::cases(8000);
  run<2>(2, 1, n);
  run<2>(12, 2, n);
  run<10>(2, 3, n);
  run<10>(12, 4, n);
  test_assert(alloc::getMemorySize() == baseline);
  return test_end();
}
