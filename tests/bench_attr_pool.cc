/* Value-pool benchmark (P1b W6). Not a ctest test: run manually in a Release
 * build. Part 1 scales the entry count at the default slot page size; part 2
 * compares slot page sizes at 1e5 entries. Prints the W6 acceptance checks. */

#include "litestl/attr/pool_store.h"

#include "splitmix64.h"

#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <vector>

using namespace litestl;
using namespace litestl::attr;

static double now_ns()
{
  return double(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count());
}

template <int S> struct Bench {
  using Pool = InternPool<uint64_t, S>;
  struct Owner {
    PoolStore store;
    AttrGroup g;
    uint32_t id;
    Owner()
    {
      id = store.add_pool<Pool>("bench.u64");
      g.add_builtin<Column<PoolRef>>("ref");
    }
    const AttrGroup *cg[1];
    AttrGroup *mg[1];
    PoolStore::Roots roots()
    {
      cg[0] = &g;
      return PoolStore::Roots{util::span<const AttrGroup *const>(cg, 1), {}, nullptr};
    }
    PoolStore::MutRoots mroots()
    {
      mg[0] = &g;
      return PoolStore::MutRoots{util::span<AttrGroup *const>(mg, 1), {}, nullptr};
    }
    Column<PoolRef> &col()
    {
      return g.builtin<Column<PoolRef>>(0);
    }
  };

  struct Row {
    uint32_t n;
    double miss, hit, find_hit, find_miss, copy_us, edit_us, collect_ms, canon_ms, write_mbs, destroy_us;
    double slot_kb, index_kb, index_bound_kb;
  };

  static uint64_t val(uint64_t i)
  {
    return i * 0x9e3779b97f4a7c15ull + 1;
  }

  static Row run(uint32_t n)
  {
    Row row{};
    row.n = n;
    Owner o;
    o.g.resize(n);
    double t0 = now_ns();
    for (uint32_t i = 0; i < n; i++) {
      o.col().set(i, o.store.template intern<Pool>(o.id, val(i)));
    }
    row.miss = (now_ns() - t0) / n;
    t0 = now_ns();
    uint64_t sink = 0;
    for (uint32_t i = 0; i < n; i++) {
      sink += o.store.template intern<Pool>(o.id, val(i)).slot;
    }
    row.hit = (now_ns() - t0) / n;
    t0 = now_ns();
    for (uint32_t i = 0; i < n; i++) {
      sink += o.store.template find<Pool>(o.id, val(i)).slot;
    }
    row.find_hit = (now_ns() - t0) / n;
    t0 = now_ns();
    for (uint32_t i = 0; i < n; i++) {
      sink += o.store.template find<Pool>(o.id, val(i + n)).slot;
    }
    row.find_miss = (now_ns() - t0) / n;

    /* snapshot copy (store + group) */
    const int reps = n >= 1000000 ? 5 : 50;
    t0 = now_ns();
    for (int r = 0; r < reps; r++) {
      Owner c(o);
      sink += c.store.pool_count();
    }
    row.copy_us = (now_ns() - t0) / reps / 1000.0;

    /* copy, then intern and assign 1% new entries */
    {
      Owner c(o);
      const Pool &before = c.store.template pool<Pool>(c.id);
      const uint64_t idx0 = before.cow_bytes();
      const uint32_t k = n / 100 > 0 ? n / 100 : 1;
      test::SplitMix64 rng(5);
      t0 = now_ns();
      for (uint32_t i = 0; i < k; i++) {
        c.col().set(uint32_t(rng.below(n)), c.store.template intern<Pool>(c.id, val(uint64_t(n) * 3 + i)));
      }
      row.edit_us = (now_ns() - t0) / 1000.0;
      const Pool &after = c.store.template pool<Pool>(c.id);
      const Pool &orig = o.store.template pool<Pool>(o.id);
      row.slot_kb = double(after.slot_page_count() - after.shared_slot_pages_with(orig)) *
                    double(Pool::slot_page_bytes()) / 1024.0;
      row.index_kb = double(after.cow_bytes() - idx0) / 1024.0;
      row.index_bound_kb = 2.0 * 8.0 * double(k) * 4.0 / 1024.0; /* delta table: <= 4x slack for its load factor */
      /* destruction of the diverged snapshot */
      t0 = now_ns();
    }
    row.destroy_us = (now_ns() - t0) / 1000.0;

    /* collect with 10% garbage */
    for (uint32_t i = 0; i < n / 10; i++) {
      o.col().set(i, kNoRef);
    }
    t0 = now_ns();
    o.store.collect(o.roots());
    row.collect_ms = (now_ns() - t0) / 1e6;

    /* canonicalize (reverse the column so everything moves) */
    for (uint32_t i = 0; i < n / 2; i++) {
      o.g.swap(i, n - 1 - i);
    }
    t0 = now_ns();
    o.store.canonicalize(o.mroots());
    row.canon_ms = (now_ns() - t0) / 1e6;

    io::ByteWriter w;
    t0 = now_ns();
    o.store.write(w, o.roots());
    row.write_mbs = double(w.size()) / ((now_ns() - t0) / 1e9) / 1e6;
    if (sink == 42) {
      printf(" ");
    }
    return row;
  }
};

int main()
{
  printf("Part 1: slot page shift %d\n", kDefaultSlotShift);
  printf("%9s %8s %8s %8s %8s %9s %9s %9s %9s %10s %10s %9s %9s %9s\n", "entries", "miss_ns", "hit_ns",
         "fhit_ns", "fmiss_ns", "copy_us", "edit1%_us", "destr_us", "slot_KB", "index_KB", "idx_bnd_KB",
         "collect", "canon", "wr_MB/s");
  Bench<kDefaultSlotShift>::Row rows[4];
  int ri = 0;
  for (uint32_t n : {1000u, 10000u, 100000u, 1000000u}) {
    const auto r = Bench<kDefaultSlotShift>::run(n);
    rows[ri++] = r;
    printf("%9u %8.1f %8.1f %8.1f %8.1f %9.1f %9.1f %9.1f %9.1f %10.1f %10.1f %7.2fms %7.2fms %9.0f\n", r.n,
           r.miss, r.hit, r.find_hit, r.find_miss, r.copy_us, r.edit_us, r.destroy_us, r.slot_kb, r.index_kb,
           r.index_bound_kb, r.collect_ms, r.canon_ms, r.write_mbs);
  }
  printf("\nPart 2: slot page shifts at 1e5 entries\n");
  printf("%6s %8s %8s %9s %9s\n", "shift", "hit_ns", "copy_us", "edit1%_us", "slot_KB");
  auto p2 = [](auto r, int s) {
    printf("%6d %8.1f %8.1f %9.1f %9.1f\n", s, r.hit, r.copy_us, r.edit_us, r.slot_kb);
  };
  p2(Bench<8>::run(100000), 8);
  p2(Bench<10>::run(100000), 10);
  p2(Bench<12>::run(100000), 12);

  printf("\nW6 acceptance:\n");
  const double hit_ratio = rows[2].hit / rows[0].hit;
  const double copy_ratio = rows[3].copy_us / rows[2].copy_us;
  printf("  intern hit 1e5 / 1e3 = %.2f (<= 3): %s\n", hit_ratio, hit_ratio <= 3 ? "ok" : "FAIL");
  printf("  copy 1e6 / 1e5 = %.2f (<= 15): %s\n", copy_ratio, copy_ratio <= 15 ? "ok" : "FAIL");
  printf("  collect at 1e6 = %.2f ms (< 50): %s\n", rows[3].collect_ms, rows[3].collect_ms < 50 ? "ok" : "FAIL");
  for (const auto &r : rows) {
    printf("  index bytes copied at %u: %.1f KB (bound %.1f KB): %s\n", r.n, r.index_kb, r.index_bound_kb,
           r.index_kb <= r.index_bound_kb ? "ok" : "FAIL");
  }
  return 0;
}
