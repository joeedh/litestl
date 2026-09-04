#include "util/error.h"
#include <string>
#include <vector>
#include <cstdio>
using namespace litestl::util;

using R = ValueOrErrors<int, "p.parse", "ExpectedMethod", "ExpectedStatement">;

constexpr R parse(int n) {
  if (n > 0) return R::ok(n * 2);
  return R::error<"ExpectedMethod">();
}
static_assert(*parse(21) == 42);
static_assert(parse(0).is<"ExpectedMethod">());
static_assert(!parse(0).success());
static_assert(parse(0).valueOr(-1) == -1);

/* constexpr with a non-trivially-destructible T */
using SR = ValueOrErrors<std::string, "p.name", "Missing">;
constexpr bool strTest() {
  auto a = SR::ok(std::string("hello world, long enough to heap allocate"));
  if (!a) return false;
  auto b = std::move(a);
  if (b->size() != 41) return false;
  b = SR::error<"Missing">();
  return b.is<"Missing">();
}
static_assert(strTest());

/* in-place construction */
using VR = ValueOrErrors<std::vector<int>, "p.list", "Empty">;
constexpr bool inPlace() {
  auto v = VR::okInPlace(5, 7);
  return v->size() == 5 && (*v)[0] == 7;
}
static_assert(inPlace());

/* triviality propagates for trivial T */
static_assert(std::is_trivially_destructible_v<R>);
static_assert(!std::is_trivially_destructible_v<SR>);
static_assert(std::is_move_constructible_v<R>);
static_assert(!std::is_copy_constructible_v<R>);

int main() {
  std::printf("sizeof(R)=%zu align=%zu | sizeof(SR)=%zu align=%zu\n",
              sizeof(R), alignof(R), sizeof(SR), alignof(SR));
  auto r = parse(5);
  std::printf("runtime ok: %d\n", *r);
  return 0;
}
