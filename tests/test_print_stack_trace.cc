#include "../util/rand.h"
#include "../util/set.h"
#include "../util/string.h"
#include "../util/vector.h"
#include "../platform/platform.h"
#include "test_util.h"
#include <cstdio>

test_init;

struct Bleh {
  ATTR_NO_OPT void A()
  {
    printf("%s\n", litestl::platform::getStackTrace().c_str());
  }
  ATTR_NO_OPT void B()
  {
    A();
  }
};

int main(void)
{
  using litestl::util::string;
  Bleh b;

  printf("=== stack trace: ===\n");
  b.B();

  test_assert(true);
  return 0;
}