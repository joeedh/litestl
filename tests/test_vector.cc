#include "../util/alloc.h"
#include "../util/vector.h"
#include "test_util.h"
#include <cstdio>

test_init;

/* Owning element type: counts live objects and detects double releases. */
static int g_live = 0, g_double_free = 0;
struct Owner {
  int *p = nullptr;
  Owner() = default;
  explicit Owner(int v) : p(new int(v))
  {
    g_live++;
  }
  Owner(Owner &&b) noexcept : p(b.p)
  {
    b.p = nullptr;
  }
  Owner &operator=(Owner &&b) noexcept
  {
    if (this != &b) {
      release();
      p = b.p;
      b.p = nullptr;
    }
    return *this;
  }
  Owner(const Owner &) = delete;
  ~Owner()
  {
    release();
  }
  void release()
  {
    if (p) {
      if (*p < 0) {
        g_double_free++;
      }
      *p = -1;
      delete p;
      p = nullptr;
      g_live--;
    }
  }
};

static void test_remove_owning()
{
  for (int remove_idx = 0; remove_idx < 5; remove_idx++) {
    for (bool swap_end : {false, true}) {
      {
        litestl::util::Vector<Owner, 2> v;
        for (int k = 0; k < 5; k++) {
          v.append(Owner(k));
        }
        test_assert(g_live == 5);
        v.remove_at(remove_idx, swap_end);
        test_assert(v.size() == 4 && g_live == 4);
        for (int k = 0; k < v.size(); k++) {
          test_assert(v[k].p != nullptr && *v[k].p != remove_idx);
        }
        if (!swap_end) {
          int expect = 0;
          for (int k = 0; k < v.size(); k++, expect++) {
            if (expect == remove_idx) {
              expect++;
            }
            test_assert(*v[k].p == expect);
          }
        }
      }
      test_assert(g_live == 0 && g_double_free == 0);
    }
  }
}

int main()
{
  test_remove_owning();
  using namespace litestl::util;

  {
    Vector<int, 32> list;

    const int size = 2048;

    for (int i = 0; i < size; i++) {
      list.append(i);
    }

    for (int i = 0; i < size; i++) {
      test_assert(list[i] == i);
    }

    int i = 0;
    for (int item : list) {
      test_assert(item == i);
      i++;
    }

    list.remove(5);
    list.append_once(5);
    test_assert(list.size() == size);
  }

  /* Regression: the move ctor branched on size_ <= static_size instead of on
   * where b's data lived, keeping b's heap capacity_ while pointing data_ at
   * the inline storage — later appends then overflowed into adjacent memory.
   * A vector hits that source state at its static_size-th append (ensure_size
   * grows at newsize == capacity_), so size 4 here is heap-backed. p.b sits
   * directly after p.a, so an overflow stomps it and the asserts catch it. */
  {
    struct PairLike {
      Vector<int, 4> a;
      Vector<int, 4> b;
    } p;

    Vector<int, 4> src;
    for (int i = 0; i < 4; i++) {
      src.append(i);
    }

    p.a = std::move(src);
    for (int i = 0; i < 4; i++) {
      p.b.append(100 + i);
    }
    for (int i = 4; i < 12; i++) {
      p.a.append(i);
    }

    for (int i = 0; i < 12; i++) {
      test_assert(p.a[i] == i);
    }
    for (int i = 0; i < 4; i++) {
      test_assert(p.b[i] == 100 + i);
    }

    /* The copy ctor had the same capacity lie. */
    Vector<int, 4> heap4;
    for (int i = 0; i < 4; i++) {
      heap4.append(i);
    }
    Vector<int, 4> copy(heap4);
    for (int i = 4; i < 12; i++) {
      copy.append(i);
    }
    for (int i = 0; i < 12; i++) {
      test_assert(copy[i] == i);
    }
  }

  return test_end();
}
