#include "litestl/util/rand.h"
#include "litestl/util/set.h"
#include "litestl/util/string.h"
#include "litestl/util/vector.h"
#include "test_util.h"
#include <cstdio>

test_init;

int main(void)
{
  using litestl::util::string;

  {
    string s = "1";
    string b = "2";
    string c = "<" + s + "," + b + ">";

    printf("%s\n", c.c_str());

    test_assert(c == string("<1,2>"));
    string d = {std::move(c)};
    test_assert(d == string("<1,2>"));

    string e;
    e = std::move(d);
    printf("%s\n", e.c_str());

    const char *longtest =
        "dsfdsfdsfasdsadsadsadsadsasdfdfdsfdsfdsfdsfsdfdfsfdsfdsfdsfdsfdsfdsf";
    const char *longtest2 =
        "dsfdsfdsfasdsadsadsadsadsasdfdfdsfdsfdsfdsfsdfdfsfdsfdsfdsfdsfdsfdsf__";

    string f = longtest;
    test_assert(f == string(longtest));

    string g = {std::move(f)};
    test_assert(g == string(longtest));

    string h;
    h = std::move(g);
    test_assert(h == string(longtest));

    string i = h + "__";
    test_assert(i == string(longtest2));
  }

  return test_end();
}