/* Pinned digest over a fixed value-pool workload (P1b): interning with inner
 * refs, garbage, collects, Temp columns and extra roots, canonicalization and
 * the owner write, at slot page shifts 2 and 10. Every build configuration
 * must reproduce it. LITESTL_PRINT_GOLDENS=1 prints the current value;
 * changing it is a deliberate act (DEC-009/DEC-015). */

#include "pool_test_types.h"

#include "splitmix64.h"
#include "test_harness.h"

#include <cinttypes>
#include <vector>

test_init;

using namespace pooltest;

template <int S> static std::vector<uint8_t> workload(hash::Hasher128 &digest)
{
  test::SplitMix64 rng(20261118);
  OwnerT<S> o(4);
  o.a.resize(500);
  o.b.resize(40);
  auto *temp = o.a.template ensure<Column<PoolRef>>("scratch", kNoRef, ColumnFlags::Temp);
  JournalRoot j;
  for (int round = 0; round < 6; round++) {
    for (int k = 0; k < 400; k++) {
      const uint32_t r = uint32_t(rng.below(500));
      const double x = double(rng.below(300)) * 0.25;
      const PoolRef src = rng.below(3) == 0 ? o.line({x * 2, -x}) : kNoRef;
      o.curve().set(r, o.line({x, x + 1.0, -0.0}, src));
      if (rng.below(10) == 0) {
        temp->set(r, o.line({1e9 + x}));
      }
    }
    for (uint32_t k = 0; k < 40; k++) {
      o.name().set(k, o.nm(rng.coin() ? "concrete" : (rng.coin() ? "glass" : "asphalt")));
    }
    j.capture(o.curve(), uint32_t(rng.below(500)));
    RefRoot *ex[] = {&j};
    test_assert(o.store.collect(o.roots(util::span<RefRoot *const>(ex, 1))));
    if (round % 2) {
      test_assert(o.store.canonicalize(o.mroots(util::span<RefRoot *const>(ex, 1))));
    }
  }
  io::ByteWriter w;
  test_assert(o.write(w));
  digest.u64(w.size());
  digest.raw_bytes(w.data(), w.size());
  return std::vector<uint8_t>(w.data(), w.data() + w.size());
}

int main()
{
  hash::Hasher128 digest(hash::DomainTag::Test);
  const auto b2 = workload<2>(digest);
  const auto b10 = workload<10>(digest);
  test_assert(b2 == b10); /* slot page size never shows in canonical bytes */
  const hash::Hash128 h = digest.finish();
  const hash::Hash128 golden = {0x73283dc9b10b45d0ull, 0x7a0914cd7544d6faull};
  if (getenv("LITESTL_PRINT_GOLDENS")) {
    printf("pool digest: {0x%016" PRIx64 "ull, 0x%016" PRIx64 "ull}\n", h.lo, h.hi);
  }
  test_assert(h == golden);
  return test_end();
}
