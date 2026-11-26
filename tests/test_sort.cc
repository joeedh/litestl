/* util::sort_unique_keys / util::stable_sort / util::group_by_key and
 * task::parallel_exclusive_scan. */

#include "../util/sort.h"
#include "../util/task.h"

#include "splitmix64.h"
#include "test_harness.h"

#include <algorithm>
#include <cstdint>
#include <vector>

test_init;

using namespace litestl;

struct Item {
  uint32_t key;
  uint32_t order; /* input position, to check stability */
};

static void test_stable_sort()
{
  test::SplitMix64 rng(42);
  test::FailureLog log("stable_sort");
  const int64_t ncases = test::cases(400);
  for (int64_t c = 0; c < ncases; c++) {
    const uint32_t n = uint32_t(rng.below(c < 40 ? 40 : 3000));
    const uint32_t key_range = uint32_t(1 + rng.below(c % 3 == 0 ? 4 : 1000)); /* many ties */
    std::vector<Item> a(n);
    for (uint32_t i = 0; i < n; i++) {
      a[i] = {uint32_t(rng.below(key_range)), i};
    }
    std::vector<Item> ref = a;
    auto less = [](const Item &x, const Item &y) { return x.key < y.key; };
    std::stable_sort(ref.begin(), ref.end(), less);
    util::stable_sort(util::span<Item>(a.data(), a.size()), less);
    bool ok = true;
    for (uint32_t i = 0; i < n; i++) {
      ok = ok && a[i].key == ref[i].key && a[i].order == ref[i].order;
    }
    log.check(ok, [&] { fprintf(stderr, "n=%u keys=%u", n, key_range); });
  }
  log.finish();
}

static void test_sort_unique_keys()
{
  test::SplitMix64 rng(7);
  std::vector<uint64_t> a(5000);
  for (size_t i = 0; i < a.size(); i++) {
    a[i] = (rng.next() << 13) | i; /* unique by construction */
  }
  auto less = [](uint64_t x, uint64_t y) { return x < y; };
  test_assert(util::sort_unique_keys(util::span<uint64_t>(a.data(), a.size()), less));
  test_assert(std::is_sorted(a.begin(), a.end()));

  /* a tie is reported, with its sorted position */
  std::vector<uint64_t> b = {9, 3, 5, 3, 1};
  size_t tie = 99;
  test_assert(!util::sort_unique_keys(util::span<uint64_t>(b.data(), b.size()), less, &tie));
  test_assert(tie == 1 && b[1] == 3 && b[2] == 3);

  std::vector<uint64_t> e;
  test_assert(util::sort_unique_keys(util::span<uint64_t>(e.data(), e.size()), less));
}

template <typename T> static void test_scan_type()
{
  test::SplitMix64 rng(99);
  const int64_t ncases = test::cases(20);
  for (int64_t c = 0; c < ncases; c++) {
    const int n = int(rng.below(c == 0 ? 1 : 200000));
    std::vector<T> in(n);
    for (int i = 0; i < n; i++) {
      in[i] = T(rng.next()); /* wraps: checks modular arithmetic */
    }
    std::vector<T> ref(n);
    T acc = 0;
    for (int i = 0; i < n; i++) {
      ref[i] = acc;
      acc = T(acc + in[i]);
    }
    for (int bands : {1, 2, 3, 4, 7, 12}) {
      for (int chunk : {1, 1000, 1 << 14}) {
        task::ScopedBandCount scope(bands);
        std::vector<T> out(n, T(0xab));
        const T total = task::parallel_exclusive_scan<T>(
            util::span<const T>(in.data(), in.size()), util::span<T>(out.data(), out.size()), chunk);
        test_assert(total == acc && out == ref);
      }
    }
    /* in place */
    std::vector<T> io = in;
    const T total = task::parallel_exclusive_scan<T>(
        util::span<const T>(io.data(), io.size()), util::span<T>(io.data(), io.size()), 777);
    test_assert(total == acc && io == ref);
  }
}

/* group_by_key against a vector of vectors: every group, in ascending index
 * order, with kNoGroup indices left out; empty groups and nkeys == 0 too */
static void test_group_by_key()
{
  test::SplitMix64 rng(7);
  test::FailureLog log("group_by_key");
  const int64_t ncases = test::cases(300);
  for (int64_t c = 0; c < ncases; c++) {
    const uint32_t n = uint32_t(rng.below(c < 30 ? 20 : 4000));
    const uint32_t nkeys = uint32_t(rng.below(c % 4 == 0 ? 3 : 600));
    util::Vector<uint32_t> key;
    key.resize(n);
    for (uint32_t i = 0; i < n; i++) {
      key[i] = (nkeys == 0 || rng.below(5) == 0) ? util::kNoGroup : uint32_t(rng.below(nkeys));
    }
    util::Vector<util::Vector<uint32_t>> ref;
    ref.resize(nkeys);
    for (uint32_t i = 0; i < n; i++) {
      if (key[i] != util::kNoGroup) {
        ref[key[i]].append(i);
      }
    }
    util::KeyGroups g;
    util::group_by_key(n, nkeys, [&](uint32_t i) { return key[i]; }, g);
    bool ok = g.size() == nkeys && g.offsets.size() == size_t(nkeys) + 1 && g.offsets[0] == 0;
    size_t total = 0;
    for (uint32_t k = 0; ok && k < nkeys; k++) {
      const auto grp = g[k];
      ok = grp.size() == ref[k].size() && std::equal(grp.begin(), grp.end(), ref[k].begin());
      total += grp.size();
    }
    ok = ok && total == g.items.size();
    log.count_case();
    log.check(ok, [&] { fprintf(stderr, "n=%u nkeys=%u", n, nkeys); });
  }
  log.finish();
  /* reuse: a second call on the same output replaces the first */
  util::KeyGroups g;
  util::group_by_key(5, 2, [](uint32_t i) { return i % 2; }, g);
  util::group_by_key(3, 1, [](uint32_t) { return 0u; }, g);
  test_assert(g.size() == 1 && g.offsets[0] == 0 && g.offsets[1] == 3 && g.items.size() == 3);
  test_assert(g[0][0] == 0 && g[0][1] == 1 && g[0][2] == 2);
}

int main()
{
  const size_t baseline = alloc::getMemorySize();
  test_stable_sort();
  test_sort_unique_keys();
  test_group_by_key();
  test_scan_type<uint32_t>();
  test_scan_type<uint64_t>();
  test_assert(alloc::getMemorySize() == baseline);
  return test_end();
}
