#include "litestl/util/alloc.h"

#include "test_harness.h"

#include <string>

test_init;

using namespace litestl;

/* Tracks construction/destruction per address so a NewArray that constructs
 * the same slot repeatedly (or skips slots) is detected. */
static int g_constructed = 0, g_destroyed = 0;

struct Tracked {
  int value;
  std::string label;
  Tracked *self;

  Tracked(int v, std::string l) : value(v), label(std::move(l)), self(this)
  {
    g_constructed++;
  }
  ~Tracked()
  {
    g_destroyed++;
    self = nullptr;
  }
};

static void test_new_array()
{
  const size_t n = 7;
  /* std::string is movable: a forwarding loop would leave later elements with
   * a moved-from (empty) label. */
  Tracked *arr = alloc::NewArray<Tracked>("test Tracked[]", n, 42, std::string("a long enough label to heap allocate"));
  test_assert(arr != nullptr);
  test_assert(g_constructed == int(n));
  for (size_t i = 0; i < n; i++) {
    test_assert(arr[i].self == &arr[i]); /* every slot constructed in place */
    test_assert(arr[i].value == 42);
    test_assert(arr[i].label == "a long enough label to heap allocate");
  }
  alloc::DeleteArray(arr, n);
  test_assert(g_destroyed == int(n));

  test_assert(alloc::NewArray<Tracked>("empty", 0, 1, std::string("x")) == nullptr);
  test_assert(g_constructed == int(n));

  /* trivially constructible elements */
  int *ints = alloc::NewArray<int>("test int[]", 5, 9);
  for (int i = 0; i < 5; i++) {
    test_assert(ints[i] == 9);
  }
  alloc::DeleteArray(ints, 5);
}

int main()
{
  test_new_array();
  return test_end();
}
