
#include "../util/index_range.h"
#include "../util/rand.h"
#include "../util/set.h"
#include "../util/string.h"
#include "../util/task.h"
#include "../util/vector.h"
#include "splitmix64.h"
#include "test_harness.h"
#include "test_util.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

test_init;

namespace {
using namespace litestl;
using util::IndexRange;

std::vector<double> sensitive_data(int64_t n)
{
  /* wide dynamic range + mixed signs: summation order changes the bits */
  test::SplitMix64 rng(99);
  std::vector<double> v(static_cast<size_t>(n));
  for (auto &x : v) {
    x = rng.finite(-40, 40);
  }
  return v;
}

uint64_t bits(double d)
{
  uint64_t u;
  memcpy(&u, &d, 8);
  return u;
}

void test_band_override_takes_effect()
{
  for (int n : {1, 2, 3, 4, 7, 12, 50}) {
    task::ScopedBandCount scope(n);
    std::atomic<int> calls{0};
    task::parallel_for(IndexRange(1000), [&](IndexRange) { calls.fetch_add(1); }, 1);
    const int expect = std::min(n, LITESTL_WORKERS_COUNT);
    if (calls.load() != expect) {
      fprintf(stderr, "band override %d: %d calls, expected %d\n", n, calls.load(), expect);
    }
    test_assert(calls.load() == expect);
  }
  /* scope restores the default */
  {
    std::atomic<int> calls{0};
    task::parallel_for(IndexRange(1000), [&](IndexRange) { calls.fetch_add(1); }, 1);
    test_assert(calls.load() == std::min(1000, task::detail::pool.worker_count()));
  }
}

void test_chunks_visit_once()
{
  for (int size : {0, 1, 7, 64, 1000, 1023}) {
    for (int chunk : {1, 3, 64, 5000}) {
      std::vector<std::atomic<int>> hits(static_cast<size_t>(size));
      std::atomic<int> bad_index{0};
      task::parallel_for_chunks(IndexRange(10, size), chunk, [&](int k, IndexRange r) {
        if (r.start != 10 + k * chunk) {
          bad_index.fetch_add(1);
        }
        for (int i : r) {
          hits[size_t(i - 10)].fetch_add(1);
        }
      });
      test_assert(bad_index.load() == 0);
      for (auto &h : hits) {
        test_assert(h.load() == 1);
      }
    }
  }
}

void test_deterministic_reduce()
{
  const int64_t n = test::cases(4000000);
  const std::vector<double> data = sensitive_data(n);
  const int chunk = 4096;
  auto map = [&](IndexRange r) {
    double s = 0;
    for (int i : r) {
      s += data[size_t(i)];
    }
    return s;
  };
  auto add = [](double a, double b) { return a + b; };

  /* serial reference: same chunking, left fold */
  double ref = 0;
  for (int64_t start = 0; start < n; start += chunk) {
    double s = 0;
    for (int64_t i = start; i < std::min<int64_t>(n, start + chunk); i++) {
      s += data[size_t(i)];
    }
    ref += s;
  }

  test::FailureLog log("parallel_reduce bit-identical across band counts");
  std::vector<uint64_t> naive_bits;
  for (int bands : {1, 2, 3, 4, 7, 12}) {
    task::ScopedBandCount scope(bands);
    log.count_case();
    const double r = task::parallel_reduce(IndexRange(int(n)), chunk, 0.0, map, add);
    log.check(bits(r) == bits(ref),
              [&] { fprintf(stderr, "bands=%d got %a ref %a", bands, r, ref); });

    /* negative control: naive per-band partial sums, folded in band order */
    std::mutex m;
    std::vector<std::pair<int, double>> partial;
    task::parallel_for(
        IndexRange(int(n)),
        [&](IndexRange r2) {
          double s = 0;
          for (int i : r2) {
            s += data[size_t(i)];
          }
          std::lock_guard g(m);
          partial.push_back({r2.start, s});
        },
        chunk);
    std::sort(partial.begin(), partial.end());
    double naive = 0;
    for (auto &p : partial) {
      naive += p.second;
    }
    naive_bits.push_back(bits(naive));
  }
  log.finish();
  std::sort(naive_bits.begin(), naive_bits.end());
  const bool control_varies = std::unique(naive_bits.begin(), naive_bits.end()) - naive_bits.begin() > 1;
  if (!control_varies) {
    fprintf(stderr, "negative control: naive band sums did not vary; data not sensitive\n");
  }
  test_assert(control_varies);
}

void test_nested_reduce()
{
  const std::vector<double> data = sensitive_data(200000);
  auto map = [&](IndexRange r) {
    double s = 0;
    for (int i : r) {
      s += data[size_t(i)];
    }
    return s;
  };
  auto add = [](double a, double b) { return a + b; };
  const double ref = task::parallel_reduce(IndexRange(200000), 1000, 0.0, map, add);
  std::atomic<int> mismatches{0};
  task::ScopedBandCount scope(4);
  task::parallel_for(
      IndexRange(8),
      [&](IndexRange r) {
        for (int i : r) {
          (void)i;
          const double v = task::parallel_reduce(IndexRange(200000), 1000, 0.0, map, add);
          if (bits(v) != bits(ref)) {
            mismatches.fetch_add(1);
          }
        }
      },
      1);
  test_assert(mismatches.load() == 0);
}
} // namespace

int main()
{
  using namespace litestl;
  using namespace litestl::util;

  /* Smoke: heavy divisible workload. */
  {
    int size = 1 << 25;

    task::parallel_for(
        IndexRange(size),
        [](IndexRange range) {
          /* unsigned: the busy-work sum wraps (signed overflow is UB) */
          volatile unsigned d = 0;

          for (int i : range) {
            d += unsigned(i >> 16);
          }
        },
        1 << 16);
  }

  /* Regression: previously-buggy non-divisible remainder path. The
   * inner-scope `IndexRange range;` used to shadow the parameter, and
   * the last chunk's size came out as `-start`. Verify every index in
   * the input range is visited exactly once across a variety of
   * (size, grain) pairs where size % grain != 0. */
  {
    const struct {
      int size;
      int grain;
    } cases[] = {
        {1, 1},
        {7, 4},
        {10, 3},
        {100, 7},
        {1001, 8},
        {12345, 64},
        {(1 << 20) + 3, 4096},
    };

    for (const auto &c : cases) {
      Vector<int> visited;
      visited.resize(c.size);
      for (int i = 0; i < c.size; i++) {
        visited[i] = 0;
      }

      /* Chunks are disjoint, so plain int writes don't race. */
      task::parallel_for(
          IndexRange(c.size),
          [&](IndexRange range) {
            test_assert(range.size >= 0);
            test_assert(range.start >= 0);
            test_assert(range.start + range.size <= c.size);
            for (int i : range) {
              visited[i]++;
            }
          },
          c.grain);

      for (int i = 0; i < c.size; i++) {
        test_assert(visited[i] == 1);
      }
    }
  }

  /* Sanity: non-zero range start. */
  {
    const int start = 100;
    const int count = 50;
    std::atomic<int> sum{0};

    task::parallel_for(
        IndexRange(start, count),
        [&](IndexRange range) {
          for (int i : range) {
            sum.fetch_add(i);
          }
        },
        7);

    int expected = 0;
    for (int i = start; i < start + count; i++) {
      expected += i;
    }
    test_assert(sum.load() == expected);
  }

  test_band_override_takes_effect();
  test_chunks_visit_once();
  test_deterministic_reduce();
  test_nested_reduce();

  return test_end();
}
