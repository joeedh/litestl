/* util::sort_unique_keys / util::stable_sort and task::parallel_exclusive_scan. */

#include "litestl/util/sort.h"
#include "litestl/util/task.h"

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

int main()
{
  const size_t baseline = alloc::getMemorySize();
  test_stable_sort();
  test_sort_unique_keys();
  test_scan_type<uint32_t>();
  test_scan_type<uint64_t>();
  test_assert(alloc::getMemorySize() == baseline);
  return test_end();
}
