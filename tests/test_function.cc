#include "math/matrix.h"
#include "test_util.h"
#include "litestl/util/function.h"
#include "litestl/util/rand.h"
#include <cstdio>

test_init;

using namespace litestl;

/* function_ref must stay trivially copyable so wrapping it in a std::function
 * stores it inline (no heap allocation) — task::parallel_for relies on this. */
static_assert(std::is_trivially_copyable_v<util::function_ref<int(int)>>,
              "function_ref must be trivially copyable");

int test(util::function_ref<int(int)> cb)
{
  return cb(-1);
}

int main()
{

  {
    test([](int val) { return val + 1; });
  }

  return test_end();
}
