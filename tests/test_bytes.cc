#include "litestl/io/bytes.h"

#include "test_harness.h"

#include <cstring>

test_init;

using namespace litestl;

int main()
{
  io::ByteWriter w;
  w.u8(0xab);
  w.u32(0x01020304u);
  w.u64(0x1122334455667788ull);
  w.i64(-5);
  w.str("hello");
  w.str("");
  const uint8_t raw[3] = {9, 8, 7};
  w.put(raw, 3);
  /* little-endian layout */
  test_assert(w.data()[1] == 0x04 && w.data()[4] == 0x01);
  test_assert(w.size() == 1 + 4 + 8 + 8 + (8 + 5) + 8 + 3);

  io::ByteReader r(w.data(), w.size());
  test_assert(r.u8() == 0xab);
  test_assert(r.u32() == 0x01020304u);
  test_assert(r.u64() == 0x1122334455667788ull);
  test_assert(r.i64() == -5);
  test_assert(r.str() == "hello");
  test_assert(r.str().empty() && !r.failed());
  const uint8_t *v = r.view(3);
  test_assert(v && v[0] == 9 && v[2] == 7);
  test_assert(r.at_end());

  /* over-read: sticky error, zero results */
  test_assert(r.u32() == 0 && r.failed() && r.remaining() == 0);
  test_assert(r.u8() == 0 && r.failed());

  /* a length prefix larger than the remaining bytes is rejected without reading */
  {
    io::ByteWriter w2;
    w2.u64(1ull << 40);
    w2.put("abc", 3);
    io::ByteReader r2(w2.data(), w2.size());
    test_assert(r2.str().empty() && r2.failed());
  }
  /* truncated scalar */
  {
    const uint8_t two[2] = {1, 2};
    io::ByteReader r3(two, 2);
    test_assert(r3.u32() == 0 && r3.failed());
  }
  /* large writes (beyond the inline buffer) */
  {
    io::ByteWriter big;
    for (uint32_t i = 0; i < 100000; i++) {
      big.u32(i);
    }
    io::ByteReader r4(big.data(), big.size());
    bool ok = true;
    for (uint32_t i = 0; i < 100000; i++) {
      ok &= r4.u32() == i;
    }
    test_assert(ok && r4.at_end());
  }
  return test_end();
}
