/* Pinned digest over a fixed litestl::attr workload (columns, groups, mixing,
 * permute, hashing, serialization). Every build configuration must
 * reproduce it. Also built as test_attr_digest_fma with -ffp-contract=fast to
 * show the mix kernels are immune to the consumer's FP-contraction flags.
 * LITESTL_PRINT_GOLDENS=1 prints the current value. Changing it is a
 * deliberate act (DEC-009/DEC-010). */

#include "../attr/bool_column.h"
#include "../attr/group.h"

#include "splitmix64.h"
#include "test_harness.h"

#include <cinttypes>
#include <vector>

test_init;

using namespace litestl;
using namespace litestl::attr;

static hash::Hash128 workload()
{
  test::SplitMix64 rng(20261118);
  hash::Hasher128 digest(hash::DomainTag::Test);
  for (int shift : {2, 12}) {
    AttrGroup g(shift);
    g.add_builtin<Column<math::double3>>("pos");
    g.add_builtin<Column<uint32_t>>(".next", 0xffffffffu, ColumnFlags::Topo);
    auto *temp = g.ensure<Column<float>>("temp", 20.0f);
    auto *id = g.ensure<Column<int32_t>>("id", -1);
    auto *sel = g.ensure<BoolColumn>("sel");
    g.resize(3000);
    for (uint32_t i = 0; i < 3000; i++) {
      const double x = rng.unit(), y = rng.unit(), z = rng.unit();
      g.builtin<Column<math::double3>>(0).set(i, math::double3(x, y, z));
      if (i % 3 == 0) {
        temp->set(i, float(rng.unit() * 40.0));
      }
      id->set(i, int32_t(i * 7));
      set_bool(*sel, i, i % 5 == 0);
    }
    /* interpolating mixes with fractional weights (FP-contraction sensitive) */
    for (int round = 0; round < 50; round++) {
      std::vector<uint32_t> targets, offsets{0}, sources;
      std::vector<double> weights;
      for (int k = 0; k < 20; k++) {
        const uint32_t t = uint32_t(round * 20 + k);
        targets.push_back(t);
        for (int q = 0; q < 3; q++) {
          sources.push_back(uint32_t(rng.below(3000)));
          weights.push_back(rng.unit() / 3.0);
        }
        offsets.push_back(uint32_t(sources.size()));
      }
      const char *err = nullptr;
      if (!g.mix(MixBatch{targets, offsets, sources, weights}, nullptr, nullptr, &err)) {
        fprintf(stderr, "mix failed: %s\n", err);
        abort();
      }
    }
    std::vector<uint32_t> perm(3000);
    for (uint32_t i = 0; i < 3000; i++) {
      perm[i] = (i % 11 == 0) ? ColumnBase::kDropped : (i * 7919u) % 3000u;
    }
    /* make it injective: (i*7919) mod 3000 is a bijection since gcd = 1 */
    g.permute(perm, 3000);
    digest.h128(g.content_hash());
    io::ByteWriter w;
    g.write(w);
    digest.raw_bytes(w.data(), w.size());
  }
  return digest.finish();
}

int main()
{
  const hash::Hash128 value = workload();
  const hash::Hash128 expected = {0x23dbdc3cd35d0630ull, 0xeb4c52c53316afafull};
  if (getenv("LITESTL_PRINT_GOLDENS")) {
    printf("attr digest = {0x%016" PRIx64 "ull, 0x%016" PRIx64 "ull}\n", value.lo, value.hi);
    fflush(stdout);
  }
  if (value != expected) {
    fprintf(stderr, "attr digest changed: %s\n", value.hex().c_str());
  }
  test_assert(value == expected);
  return test_end();
}
