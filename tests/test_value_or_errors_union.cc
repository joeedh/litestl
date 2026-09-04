#include "util/error.h"
#include <cassert>
#include <cstdio>
#include <string>

using namespace litestl::util;

using NodeId = int;

static constexpr StrLiteral ExpectedMethod{"ExpectedMethod"};
static constexpr StrLiteral ExpectedStatement{"ExpectedStatement"};

using MethodErrors =
    ValueOrErrors<NodeId, "parser.parseMethod", "ExpectedMethod", "ExpectedStatement">;

MethodErrors parseMethod(bool good, bool stmt)
{
  if (good) {
    NodeId nodeid = 42;
    return MethodErrors::ok(nodeid);
  }
  if (stmt) {
    return MethodErrors::error<"ExpectedStatement">();
  }
  return MethodErrors::error<"ExpectedMethod">();
}

/* non-trivial payload, to check destruction */
static int live = 0;
struct Tracked {
  std::string s;
  Tracked(std::string v) : s(std::move(v)) { live++; }
  Tracked(Tracked &&o) noexcept : s(std::move(o.s)) { live++; }
  ~Tracked() { live--; }
};
using TrackedResult = ValueOrErrors<Tracked, "test.tracked", "Nope">;

int main()
{
  auto a = parseMethod(true, false);
  assert(a.success());
  assert(*a == 42);

  auto b = parseMethod(false, false);
  assert(!b);
  assert(b.is<"ExpectedMethod">());
  assert(!b.is<"ExpectedStatement">());
  assert(b.is(ExpectedMethod));
  assert(!b.is(ExpectedStatement));
  assert(b.errorName() == "ExpectedMethod");

  auto c = parseMethod(false, true);
  assert(c.is<"ExpectedStatement">());
  assert(c.errorIndex() == 1);
  assert(MethodErrors::owner == "parser.parseMethod");
  assert(MethodErrors::error_count == 2);
  static_assert(MethodErrors::has_error<"ExpectedMethod">);
  static_assert(!MethodErrors::has_error<"Bogus">);

  /* move semantics + destruction */
  {
    auto t = TrackedResult::ok(Tracked{"hello"});
    assert(live == 1);
    auto t2 = std::move(t);
    assert(t2->s == "hello");
    t2 = TrackedResult::error<"Nope">();
    assert(t2.is<"Nope">());
  }
  assert(live == 0);

  assert(b.valueOr(-1) == -1);
  assert(a.valueOr(-1) == 42);

  std::printf("all ok, sizeof(MethodErrors)=%zu align=%zu\n",
              sizeof(MethodErrors),
              alignof(MethodErrors));
  return 0;
}

/* extra: constexpr + trivial-destructor checks appended below main */