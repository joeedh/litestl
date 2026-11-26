#include "../util/rng.h"

#include "test_harness.h"

#include <cinttypes>
#include <cmath>
#include <vector>

test_init;

using namespace litestl;
using rng::Counter;
using rng::Key;
using rng::Stream;

/* Known-answer vectors from Random123 tests/kat_vectors (BSD-3-Clause,
 * D. E. Shaw Research): philox4x32 R ctr[4] key[2] -> out[4]. */
struct Kat {
  int rounds;
  Counter ctr;
  Key key;
  Counter out;
};
static const Kat kats[] = {
    {7, {0, 0, 0, 0}, {0, 0}, {0x5f6fb709, 0x0d893f64, 0x4f121f81, 0x4f730a48}},
    {7,
     {0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff},
     {0xffffffff, 0xffffffff},
     {0x5207ddc2, 0x45165e59, 0x4d8ee751, 0x8c52f662}},
    {7,
     {0x243f6a88, 0x85a308d3, 0x13198a2e, 0x03707344},
     {0xa4093822, 0x299f31d0},
     {0x4dfccaba, 0x190a87f0, 0xc47362ba, 0xb6b5242a}},
    {10, {0, 0, 0, 0}, {0, 0}, {0x6627e8d5, 0xe169c58d, 0xbc57ac4c, 0x9b00dbd8}},
    {10,
     {0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff},
     {0xffffffff, 0xffffffff},
     {0x408f276d, 0x41c83b0e, 0xa20bc7c6, 0x6d5451fd}},
    {10,
     {0x243f6a88, 0x85a308d3, 0x13198a2e, 0x03707344},
     {0xa4093822, 0x299f31d0},
     {0xd16cfe09, 0x94fdcceb, 0x5001e420, 0x24126ea1}},
};

static void test_kats()
{
  for (const Kat &k : kats) {
    const Counter out = k.rounds == 7 ? rng::philox4x32<7>(k.ctr, k.key) :
                                        rng::philox4x32<10>(k.ctr, k.key);
    test_assert(out == k.out);
  }
  static_assert(rng::philox4x32<10>({0, 0, 0, 0}, {0, 0})[0] == 0x6627e8d5);
}

static const Stream stream(hash::Hasher128(hash::DomainTag::Test).str("rng-golden").finish());

/* Pinned stream outputs (determinism contract). LITESTL_PRINT_GOLDENS=1 prints
 * current values. */
static void test_stream_goldens()
{
  struct G {
    const char *name;
    uint64_t value, expected;
  };
  const uint64_t e = 0x123456789abcdefull;
  G g[] = {
      {"u32(e,0)", stream.u32(e, 0), 0x000000008fa2b6f0ull},
      {"u32(e,1)", stream.u32(e, 1), 0x00000000bd5052c2ull},
      {"u64(e,3)", stream.u64(e, 3), 0xfe59c1e447e8f6dbull},
      {"uniform(e,0) bits", std::bit_cast<uint64_t>(stream.uniform(e, 0)), 0x3fe82baa5691f456ull},
      {"uniformf(e,0) bits", std::bit_cast<uint32_t>(stream.uniformf(e, 0)), 0x000000003f0fa2b6ull},
      {"range(e,-5,17)", uint64_t(stream.range(e, -5, 17)), 0x000000000000000bull},
      /* high-rejection span: ~half the 64-bit words are rejected */
      {"range(e,-2,INT64_MAX) span 2^63+1", uint64_t(stream.range(e, -2, INT64_MAX)), 0x60aea95a47d15b76ull},
      {"range(7,0,2^31+1,9)", uint64_t(stream.range(7, 0, (int64_t(1) << 31) + 1, 9)), 0x0000000061e7e311ull},
      {"element_of", Stream::element_of(hash::Hash128{1, 2}), 0x3abf2a20650683e6ull},
  };
  const bool print = getenv("LITESTL_PRINT_GOLDENS") != nullptr;
  for (auto &x : g) {
    if (print) {
      printf("      {\"%s\", ..., 0x%016" PRIx64 "ull},\n", x.name, x.value);
    }
    if (x.value != x.expected) {
      fprintf(stderr, "golden %s changed: %016" PRIx64 "\n", x.name, x.value);
    }
    test_assert(x.value == x.expected);
  }
}

static void test_independence_of_draws()
{
  /* range() rejection must not reuse the bits of draw+1 */
  int64_t equal = 0;
  for (uint64_t e = 0; e < 1000; e++) {
    equal += stream.u32(e, 1) == stream.block(e, 0, 1)[0];
  }
  test_assert(equal < 3);
}

static void test_distribution()
{
  const int bins = 256;
  const int64_t n = test::cases(int64_t(1) << 20);
  std::vector<int64_t> counts(bins, 0);
  double sum = 0, sum2 = 0;
  for (int64_t i = 0; i < n; i++) {
    counts[stream.u32(uint64_t(i)) >> 24]++;
    const double u = stream.uniform(uint64_t(i), 5);
    test_assert(u >= 0.0 && u < 1.0);
    sum += u;
    sum2 += u * u;
  }
  const double expect = double(n) / bins;
  double chi2 = 0;
  for (int64_t c : counts) {
    chi2 += (c - expect) * (c - expect) / expect;
  }
  /* 255 dof: mean 255, sd ~22.6; 400 is > 6 sd */
  printf("chi2=%.1f mean=%.5f var=%.5f\n", chi2, sum / n, sum2 / n - (sum / n) * (sum / n));
  test_assert(chi2 < 400.0);
  test_assert(std::fabs(sum / n - 0.5) < 0.005);
  test_assert(std::fabs(sum2 / n - (sum / n) * (sum / n) - 1.0 / 12.0) < 0.005);
}

static void test_range()
{
  test::FailureLog log("rng range bounds/coverage");
  for (int64_t span : {int64_t(1), int64_t(2), int64_t(3), int64_t(7), int64_t(1000)}) {
    std::vector<int> seen(size_t(span), 0);
    for (uint64_t e = 0; e < 20000; e++) {
      log.count_case();
      const int64_t v = stream.range(e, -3, -3 + span);
      if (!log.check(v >= -3 && v < -3 + span,
                     [&] { fprintf(stderr, "span %" PRId64 " v %" PRId64, span, v); }))
      {
        continue;
      }
      seen[size_t(v + 3)] = 1;
    }
    for (int s : seen) {
      log.check(s == 1, [&] { fprintf(stderr, "span %" PRId64 " missing value", span); });
    }
  }
  /* full-width-ish spans stay in range */
  for (uint64_t e = 0; e < 1000; e++) {
    const int64_t v = stream.range(e, INT64_MIN, INT64_MAX);
    log.check(v != INT64_MAX, [] {});
  }
  log.finish();
}

int main()
{
  test_kats();
  test_stream_goldens();
  test_independence_of_draws();
  test_distribution();
  test_range();
  return test_end();
}
