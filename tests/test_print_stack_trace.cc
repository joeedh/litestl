#include "litestl/util/rand.h"
#include "litestl/util/set.h"
#include "litestl/util/string.h"
#include "litestl/util/vector.h"
#include "test_util.h"
#include <cstdio>
#include "platform/platform.h"

test_init;

struct Bleh {
  ATTR_NO_OPT void A() {
    printf("%s\n", litestl::platform::getStackTrace().c_str());
  }
  ATTR_NO_OPT void B() {
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