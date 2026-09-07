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

  {
    // Appending one character at a time must reallocate a logarithmic number of times.
    string s;
    int reallocations = 0;
    const char *last = s.c_str();
    for (int i = 0; i < 100000; i++) {
      s += char('a' + (i % 26));
      if (s.c_str() != last) {
        reallocations++;
        last = s.c_str();
      }
    }
    test_assert(s.size() == 100000);
    test_assert(reallocations < 40);
    test_assert(s.capacity() >= s.size());
    test_assert(s[99999] == char('a' + (99999 % 26)));
    test_assert(s.c_str()[100000] == 0);

    string moved = std::move(s);
    test_assert(moved.size() == 100000);
    test_assert(moved.capacity() >= moved.size());
    moved += "tail";
    test_assert(moved.ends_with(string("tail")));
  }

  return test_end();
}