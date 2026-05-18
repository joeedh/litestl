#include "litestl/util/pool.h"
#include "test_util.h"

test_init;

namespace {

struct Counted {
  static int live;
  int value;
  /* Pad so sizeof(Counted) >= sizeof(void*), required by Pool's
   * intrusive freelist. */
  void *pad;
  Counted(int v) : value(v), pad(nullptr)
  {
    live++;
  }
  Counted(const Counted &) = delete;
  Counted(Counted &&b) noexcept : value(b.value), pad(b.pad)
  {
    live++;
  }
  ~Counted()
  {
    live--;
  }
};
int Counted::live = 0;

} // namespace

int main()
{
  using namespace litestl::util;

  /* Basic alloc / release. */
  {
    Pool<Counted, 4> pool;
    test_assert(Counted::live == 0);
    test_assert(pool.live_count() == 0);
    Counted *a = pool.alloc(10);
    Counted *b = pool.alloc(20);
    Counted *c = pool.alloc(30);
    test_assert(a->value == 10);
    test_assert(b->value == 20);
    test_assert(c->value == 30);
    test_assert(Counted::live == 3);
    test_assert(pool.live_count() == 3);
    pool.release(b);
    test_assert(Counted::live == 2);
    test_assert(pool.live_count() == 2);

    /* Freelist reuse: next alloc should land in the freed slot. */
    Counted *d = pool.alloc(40);
    test_assert(d == b);
    test_assert(d->value == 40);
    test_assert(Counted::live == 3);
    (void)a;
    (void)c;
  }
  /* Pool destructor must destroy remaining live objects. */
  test_assert(Counted::live == 0);

  /* Cross-slab allocation and reuse. */
  {
    Pool<Counted, 2> pool;
    Counted *p0 = pool.alloc(0);
    Counted *p1 = pool.alloc(1);
    Counted *p2 = pool.alloc(2); // forces new slab
    Counted *p3 = pool.alloc(3);
    Counted *p4 = pool.alloc(4); // forces another slab
    test_assert(Counted::live == 5);
    pool.release(p1);
    pool.release(p3);
    test_assert(Counted::live == 3);
    Counted *q0 = pool.alloc(31);
    Counted *q1 = pool.alloc(33);
    /* Either ordering is fine, just check we got the freed slots. */
    test_assert(q0 == p1 || q0 == p3);
    test_assert(q1 == p1 || q1 == p3);
    test_assert(q0 != q1);
    test_assert(Counted::live == 5);
    (void)p0;
    (void)p2;
    (void)p4;
  }
  test_assert(Counted::live == 0);

  /* Move construction. */
  {
    Pool<Counted, 4> a;
    a.alloc(1);
    a.alloc(2);
    test_assert(Counted::live == 2);
    Pool<Counted, 4> b(std::move(a));
    test_assert(b.live_count() == 2);
    test_assert(a.live_count() == 0);
    test_assert(Counted::live == 2);
  }
  test_assert(Counted::live == 0);

  /* clear(). */
  {
    Pool<Counted, 4> pool;
    pool.alloc(1);
    pool.alloc(2);
    pool.alloc(3);
    test_assert(Counted::live == 3);
    pool.clear();
    test_assert(Counted::live == 0);
    test_assert(pool.live_count() == 0);
    /* Pool is still usable after clear. */
    pool.alloc(99);
    test_assert(Counted::live == 1);
  }
  test_assert(Counted::live == 0);

  return test_end();
}
