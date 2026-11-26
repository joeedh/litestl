#include "../util/hash128.h"

#include "data/siphash128_vectors.h"
#include "splitmix64.h"
#include "test_harness.h"

#include <cinttypes>
#include <cmath>
#include <limits>
#include <vector>

test_init;

using namespace litestl;
using hash::DomainTag;
using hash::Hash128;
using hash::Hasher128;

struct ByteSink {
  std::vector<uint8_t> bytes;
  void put(const void *p, size_t n)
  {
    const uint8_t *b = static_cast<const uint8_t *>(p);
    bytes.insert(bytes.end(), b, b + n);
  }
  bool equals(std::initializer_list<uint8_t> expected) const
  {
    return bytes == std::vector<uint8_t>(expected);
  }
};

static void test_official_vectors()
{
  test::FailureLog log("siphash-2-4-128 official vectors");
  uint8_t key_bytes[16], msg[64];
  for (int i = 0; i < 16; i++) {
    key_bytes[i] = uint8_t(i);
  }
  for (int i = 0; i < 64; i++) {
    msg[i] = uint8_t(i);
  }
  const Hash128 key = Hash128::from_le_bytes(key_bytes);
  for (int n = 0; n < 64; n++) {
    log.count_case();
    uint8_t out[16];
    hash::hash128(msg, size_t(n), key).to_le_bytes(out);
    log.check(memcmp(out, kSipHash128Vectors[n], 16) == 0,
              [&] { fprintf(stderr, "message length %d", n); });
  }
  log.finish();
}

static void test_streaming()
{
  test::FailureLog log("siphash streaming == one-shot");
  test::SplitMix64 rng(7);
  uint8_t data[300];
  for (auto &b : data) {
    b = uint8_t(rng.next());
  }
  for (int c = 0; c < 2000; c++) {
    log.count_case();
    const size_t n = rng.below(sizeof(data) + 1);
    const Hash128 expect = hash::hash128(data, n);
    hash::SipHash128 h;
    size_t pos = 0;
    while (pos < n) {
      size_t step = std::min<size_t>(n - pos, rng.below(20));
      h.put(data + pos, step);
      pos += step;
    }
    log.check(h.finish() == expect, [&] { fprintf(stderr, "n=%zu", n); });
  }
  log.finish();
}

static void test_encoders()
{
  using namespace hash::canonical;
  {
    ByteSink s;
    u32(s, 0x01020304u);
    test_assert(s.equals({TAG_U32, 0x04, 0x03, 0x02, 0x01}));
  }
  {
    ByteSink s;
    i32(s, -2);
    test_assert(s.equals({TAG_I32, 0xfe, 0xff, 0xff, 0xff}));
  }
  {
    ByteSink s;
    u64(s, 0x0102030405060708ull);
    test_assert(s.equals({TAG_U64, 8, 7, 6, 5, 4, 3, 2, 1}));
  }
  {
    ByteSink s;
    i64(s, -1);
    test_assert(s.equals({TAG_I64, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}));
  }
  {
    ByteSink s;
    u8(s, 0xab);
    boolean(s, true);
    test_assert(s.equals({TAG_U8, 0xab, TAG_BOOL, 1}));
  }
  {
    ByteSink s;
    f32_bits(s, 1.0f); /* 0x3f800000 */
    test_assert(s.equals({TAG_F32_BITS, 0x00, 0x00, 0x80, 0x3f}));
  }
  {
    ByteSink s;
    f64_bits(s, -0.0);
    test_assert(s.equals({TAG_F64_BITS, 0, 0, 0, 0, 0, 0, 0, 0x80}));
  }
  {
    ByteSink a, b;
    f64_canonical(a, -0.0);
    f64_canonical(b, 0.0);
    test_assert(a.bytes == b.bytes);
    test_assert(a.equals({TAG_F64_CANONICAL, 0, 0, 0, 0, 0, 0, 0, 0}));
  }
  {
    /* all NaNs canonicalize to 0x7ff8000000000000; raw bits preserve payloads */
    const double qnan = std::numeric_limits<double>::quiet_NaN();
    const double payload_nan = std::bit_cast<double>(0xfff0000000000123ull);
    ByteSink a, b, c, d;
    f64_canonical(a, qnan);
    f64_canonical(b, payload_nan);
    test_assert(a.bytes == b.bytes);
    test_assert(a.equals({TAG_F64_CANONICAL, 0, 0, 0, 0, 0, 0, 0xf8, 0x7f}));
    f64_bits(c, payload_nan);
    f64_bits(d, qnan);
    test_assert(c.bytes != d.bytes);
  }
  {
    ByteSink s;
    str(s, "ab");
    test_assert(s.equals({TAG_STR, 2, 0, 0, 0, 0, 0, 0, 0, 'a', 'b'}));
  }
  {
    ByteSink s;
    h128(s, Hash128{0x0807060504030201ull, 0x100f0e0d0c0b0a09ull});
    test_assert(s.equals({TAG_H128, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}));
  }
  {
    ByteSink s;
    domain(s, DomainTag::Test);
    test_assert(s.equals({TAG_DOMAIN, 2, 0, 0, 0, 0, 0, 0, 0}));
  }
}

static void test_structure()
{
  /* type tags prevent structural collisions */
  const Hash128 a = Hasher128(DomainTag::Test).u32(1).u32(2).finish();
  const Hash128 b = Hasher128(DomainTag::Test).u64((uint64_t(2) << 32) | 1).finish();
  test_assert(a != b);
  /* domains separate otherwise-identical tuples */
  const Hash128 c = Hasher128(DomainTag::Content).u32(1).u32(2).finish();
  test_assert(a != c);
  /* ordering: hi first, then lo */
  test_assert((Hash128{5, 1} <=> Hash128{0, 2}) < 0);
  test_assert((Hash128{1, 2} <=> Hash128{0, 2}) > 0);
  /* bytes and hex round trips */
  const Hash128 h{0x0123456789abcdefull, 0xfedcba9876543210ull};
  uint8_t bytes[16];
  h.to_le_bytes(bytes);
  test_assert(Hash128::from_le_bytes(bytes) == h);
  test_assert(strcmp(h.hex().c_str(), "fedcba98765432100123456789abcdef") == 0);
}

/* Regression goldens for representative ID derivations. These are part of
 * the determinism contract (DEC-009 in UberTopo): changing them changes every
 * derived ID, and requires a deliberate decision. Run with
 * LITESTL_PRINT_GOLDENS=1 to print the current values. */
static void test_goldens()
{
  struct Golden {
    const char *name;
    Hash128 value;
    Hash128 expected;
  };
  const Hash128 seed = Hasher128(DomainTag::Test).str("world").u64(42).finish();
  Golden goldens[] = {
      {"seed", seed, {0xdb9c47403735fc28ull, 0x3d94ed5e3bc8b333ull}},
      {"op_split_L", Hasher128(DomainTag(1000)).h128(seed).u32(7).u8(0).finish(), {0x55497d2af45f0ce8ull, 0x345cb3d9b7a2487cull}},
      {"op_split_R", Hasher128(DomainTag(1000)).h128(seed).u32(7).u8(1).finish(), {0x5029a08e91dd2205ull, 0x0411ba61a8276921ull}},
      {"position",
       Hasher128(DomainTag(1001)).h128(seed).f64_canonical(-0.0).f64_canonical(1.5).finish(),
       {0x1db2d2513a90634eull, 0xf370b985879de497ull}},
      {"content",
       Hasher128(DomainTag::Content).f64_bits(-0.0).raw_bytes("abc", 3).finish(),
       {0xa159f666d3ab1e44ull, 0xbc9474b5a3263aabull}},
  };
  const bool print = getenv("LITESTL_PRINT_GOLDENS") != nullptr;
  for (auto &g : goldens) {
    if (print) {
      printf("      {\"%s\", ..., {0x%016" PRIx64 "ull, 0x%016" PRIx64 "ull}},\n",
             g.name,
             g.value.lo,
             g.value.hi);
    }
    if (g.value != g.expected) {
      fprintf(stderr, "golden %s changed: %s\n", g.name, g.value.hex().c_str());
    }
    test_assert(g.value == g.expected);
  }
}

int main()
{
  test_official_vectors();
  test_streaming();
  test_encoders();
  test_structure();
  test_goldens();
  return test_end();
}
