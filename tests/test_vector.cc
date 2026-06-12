#include "test_util.h"
#include "litestl/util/alloc.h"
#include "litestl/util/vector.h"
#include <cstdio>

test_init;

int main()
{
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
