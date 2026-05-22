
#include "test_util.h"
#include "litestl/util/index_range.h"
#include "litestl/util/rand.h"
#include "litestl/util/set.h"
#include "litestl/util/string.h"
#include "litestl/util/task.h"
#include "litestl/util/vector.h"
#include <atomic>
#include <cstdio>

test_init;

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
          volatile int d = 0;

          for (int i : range) {
            d += i >> 16;
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

  return test_end();
}
