/* Page-size benchmark for litestl::attr (P1 W8 / OI-3). Not a ctest test:
 * run manually in a Release build. Prints one row per page shift. */

#include "litestl/attr/column.h"

#include "splitmix64.h"

#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <vector>

using namespace litestl;
using namespace litestl::attr;

static double now_ns()
{
  return double(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count());
}

volatile double g_sink = 0;

int main()
{
  const uint32_t N = 1u << 22; /* 4M floats */
  printf("%-6s %10s %10s %10s %10s %12s %12s %10s %12s\n",
         "shift",
         "seq_span",
         "seq_idx",
         "gather",
         "write1st",
         "cow_bytes/e",
         "cluster_ms",
         "hash_ms",
         "small_bytes");
  for (int shift : {10, 12, 14}) {
    Column<float> c(0.0f, shift);
    c.resize(N);
    test::SplitMix64 rng(1);

    double t0 = now_ns();
    for (uint32_t i = 0; i < N; i++) {
      c.write(i) = float(i & 1023);
    }
    const double write1st = (now_ns() - t0) / N;

    double acc = 0;
    t0 = now_ns();
    c.for_each_page([&](util::span<const float> s, uint32_t) {
      for (float v : s) {
        acc += v;
      }
    });
    const double seq_span = (now_ns() - t0) / N;

    t0 = now_ns();
    for (uint32_t i = 0; i < N; i++) {
      acc += c[i];
    }
    const double seq_idx = (now_ns() - t0) / N;

    std::vector<uint32_t> idx(N);
    for (auto &x : idx) {
      x = uint32_t(rng.below(N));
    }
    t0 = now_ns();
    for (uint32_t i = 0; i < N; i++) {
      acc += c[idx[i]];
    }
    const double gather = (now_ns() - t0) / N;

    /* COW amplification: bytes copied per scattered single-element edit */
    const uint32_t edits = 1000;
    double cow_bytes;
    {
      auto snap = c.share();
      uint32_t copied_pages = 0;
      for (uint32_t k = 0; k < edits; k++) {
        const uint32_t i = uint32_t(rng.below(N));
        const uint32_t p = i >> shift;
        if (c.store.shares_buffer_with(static_cast<Column<float> &>(*snap).store, p)) {
          copied_pages++;
        }
        c.write(i) = 1.0f;
      }
      cow_bytes = double(copied_pages) * double(1u << shift) * sizeof(float) / edits;
    }

    /* clustered edit: 1% of elements in clusters of 64 after a snapshot */
    t0 = now_ns();
    {
      auto snap = c.share();
      for (uint32_t k = 0; k < N / 100 / 64; k++) {
        const uint32_t base = uint32_t(rng.below(N - 64));
        for (uint32_t q = 0; q < 64; q++) {
          c.write(base + q) = 2.0f;
        }
      }
    }
    const double cluster_ms = (now_ns() - t0) / 1e6;

    t0 = now_ns();
    hash::Hasher128 h(hash::DomainTag::Content);
    c.hash_into(h);
    g_sink = double(h.finish().lo);
    const double hash_ms = (now_ns() - t0) / 1e6;

    /* small group: one Dense element, partial tail */
    Column<float> small(0.0f, shift);
    small.resize(1);
    small.set(0, 1.0f);
    const size_t before = alloc::getMemorySize();
    auto small_snap = small.share();
    small.set(0, 2.0f); /* forces one tail copy */
    const size_t small_bytes = alloc::getMemorySize() - before;

    g_sink = acc;
    printf("%-6d %10.2f %10.2f %10.2f %10.2f %12.0f %12.2f %10.2f %12zu\n",
           shift,
           seq_span,
           seq_idx,
           gather,
           write1st,
           cow_bytes,
           cluster_ms,
           hash_ms,
           small_bytes);
  }
  printf("(times in ns/element unless noted; cow_bytes/e = bytes copied per scattered edit)\n");
  return 0;
}
