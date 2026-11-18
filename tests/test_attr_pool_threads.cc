/* Threaded snapshot stress for value pools (P1b W5; run under TSan in the
 * sanitizer configuration): one base owner, N threads each copying it and
 * diverging (intern, assign, collect, canonicalize, nested copies, destroy).
 * Slot pages, index tables and nodes are shared across threads through
 * atomic counts. At the end the base must be byte-identical and the
 * process-wide allocation size back at its baseline (worker-thread leaks are
 * invisible to the per-thread block lists). */

#include "pool_test_types.h"

#include "splitmix64.h"
#include "test_harness.h"

#include <atomic>
#include <thread>
#include <vector>

test_init;

using namespace pooltest;

static std::vector<uint8_t> bytes_of(const Owner &o)
{
  io::ByteWriter w;
  o.write(w);
  return std::vector<uint8_t>(w.data(), w.data() + w.size());
}

int main()
{
  const size_t baseline = alloc::getMemorySize();
  {
    Owner base;
    base.a.resize(3000);
    for (uint32_t i = 0; i < 3000; i++) {
      base.curve().set(i, base.line({double(i % 700)}, i % 3 ? base.line({double(i % 11)}) : kNoRef));
    }
    test_assert(base.store.collect(base.roots()));
    const std::vector<uint8_t> before = bytes_of(base);

    const int nthreads = 4;
    const int64_t iters = test::cases(300);
    std::atomic<int> failures{0};
    std::vector<std::thread> ts;
    for (int t = 0; t < nthreads; t++) {
      ts.emplace_back([&, t] {
        test::SplitMix64 rng(uint64_t(t) + 17);
        Owner mine(base); /* shares pages with base and the other threads */
        for (int64_t it = 0; it < iters; it++) {
          for (int k = 0; k < 8; k++) {
            const uint32_t r = uint32_t(rng.below(mine.a.size()));
            mine.curve().set(r, mine.line({double(rng.below(900))}, rng.coin() ? mine.line({1.0 * t}) : kNoRef));
          }
          switch (rng.below(5)) {
            case 0:
              if (!mine.store.collect(mine.roots())) {
                failures++;
              }
              break;
            case 1:
              if (!mine.store.canonicalize(mine.mroots())) {
                failures++;
              }
              break;
            case 2: {
              Owner nested(mine); /* copy and drop on this thread */
              nested.curve().set(0, nested.line({-1.0}));
              nested.store.collect(nested.roots());
              break;
            }
            case 3:
              mine = base; /* re-share the base */
              break;
            default:
              break;
          }
        }
        if (!mine.store.audit(mine.roots(), false)) {
          failures++;
        }
      });
    }
    for (auto &th : ts) {
      th.join();
    }
    test_assert(failures.load() == 0);
    test_assert(base.store.audit(base.roots(), true));
    test_assert(bytes_of(base) == before);
  }
  test_assert(alloc::getMemorySize() == baseline);
  return test_end();
}
