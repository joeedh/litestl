#include "litestl/util/alloc.h"
#include "litestl/util/boolvector.h"
#include "test_util.h"
#include <cstdio>

test_init;

int main()
{
  using namespace litestl::util;

  {
    volatile BoolVector<32> list;
  }

  return test_end();
}
