#include "litestl/math/wideint.h"
#include "litestl/util/memory.h"
#include "litestl/util/rc_buffer.h"

#include "test_harness.h"

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

test_init;

using namespace litestl;
using util::RcBuffer;

struct alignas(64) Wide64 {
  double v[8];
};

static void test_basics()
{
  RcBuffer<int> empty;
  test_assert(!empty && empty.use_count() == 0 && empty.capacity() == 0 && empty.data() == nullptr);
  test_assert(!RcBuffer<int>::make(0));

  RcBuffer<int> a = RcBuffer<int>::make(10);
  test_assert(a && a.unique() && a.use_count() == 1 && a.capacity() == 10);
  for (int i = 0; i < 10; i++) {
    a.data()[i] = i * i;
  }
  {
    RcBuffer<int> b = a;
    test_assert(!a.unique() && a.use_count() == 2 && b.id() == a.id());
    RcBuffer<int> c = std::move(b);
    test_assert(!b && a.use_count() == 2);
    c = c; /* self-assignment */
    test_assert(a.use_count() == 2);
  }
  test_assert(a.unique());

  RcBuffer<int> d = RcBuffer<int>::copy_of(a.data(), 10, 16);
  test_assert(d.capacity() == 16 && d.id() != a.id());
  for (int i = 0; i < 10; i++) {
    test_assert(d.data()[i] == i * i);
  }
  d = a;
  test_assert(d.id() == a.id() && a.use_count() == 2);
  d.reset();
  test_assert(!d && a.unique());
}

template <typename T> static bool aligned(const T *p, size_t a)
{
  return (reinterpret_cast<uintptr_t>(p) & (a - 1)) == 0;
}

static void test_alignment()
{
  for (uint32_t n = 1; n < 40; n++) {
    auto a = RcBuffer<double>::make(n);
    auto b = RcBuffer<math::Int256>::make(n);
    auto c = RcBuffer<Wide64>::make(n);
    auto d = RcBuffer<uint8_t>::make(n);
    test_assert(aligned(a.data(), 16) && aligned(b.data(), 16) && aligned(c.data(), 64) &&
                aligned(d.data(), 16));
  }
}

/* Many threads copying and destroying handles to one buffer (run under TSan
 * in the sanitizer configuration). */
static void test_concurrent_refcount()
{
  RcBuffer<uint64_t> buf = RcBuffer<uint64_t>::make(64);
  for (int i = 0; i < 64; i++) {
    buf.data()[i] = uint64_t(i);
  }
  const int threads = 8;
  const int64_t iters = test::cases(200000);
  std::atomic<int64_t> bad{0};
  std::vector<std::thread> pool;
  for (int t = 0; t < threads; t++) {
    pool.emplace_back([&, t] {
      RcBuffer<uint64_t> local = buf;
      for (int64_t i = 0; i < iters; i++) {
        RcBuffer<uint64_t> a = local;
        RcBuffer<uint64_t> b = std::move(a);
        if (b.data()[(i + t) & 63] != uint64_t((i + t) & 63)) {
          bad.fetch_add(1);
        }
      }
    });
  }
  for (auto &th : pool) {
    th.join();
  }
  test_assert(bad.load() == 0);
  test_assert(buf.unique());
}

struct Base {
  int base_value = 1;
  virtual ~Base() = default;
  virtual int value() const
  {
    return base_value;
  }
};

static int g_derived_destroyed = 0;

struct Derived : Base {
  int extra = 41;
  ~Derived() override
  {
    g_derived_destroyed++;
  }
  int value() const override
  {
    return base_value + extra;
  }
};

static void test_unique_ptr()
{
  util::UniquePtr<Derived> d = util::make_unique<Derived>("test Derived");
  test_assert(d && d->value() == 42);
  util::UniquePtr<Base> b = std::move(d);
  test_assert(!d && b && b->value() == 42);
  util::UniquePtr<Base> c;
  c = std::move(b);
  test_assert(!b && c->value() == 42);
  c.reset();
  test_assert(!c && g_derived_destroyed == 1);
  {
    auto e = util::make_unique<Derived>("test Derived 2");
  }
  test_assert(g_derived_destroyed == 2);
  Derived *raw = util::make_unique<Derived>("test Derived 3").release();
  util::UniquePtr<Base> f(raw);
  f.reset();
  test_assert(g_derived_destroyed == 3);
}

int main()
{
  const size_t baseline = alloc::getMemorySize();
  test_basics();
  test_alignment();
  test_concurrent_refcount();
  test_unique_ptr();
  /* worker-thread allocations are invisible to the per-thread block lists */
  test_assert(alloc::getMemorySize() == baseline);
  return test_end();
}
