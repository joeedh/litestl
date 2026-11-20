#include "binding/binding.h"
#include "test_util.h"

#include <cstdio>
#include <cstring>

test_init;

using namespace litestl;
using namespace litestl::binding;

struct Foo {
  int counter = 0;

  int add(int a, int b)
  {
    counter++;
    return a + b;
  }

  int sum() const
  {
    return counter;
  }

  void bump(int n)
  {
    counter += n;
  }

  static binding::types::Struct<Foo> *defineBindings()
  {
    // tell allocator the binding system's allocations are not leaks
    alloc::PermanentGuard guard;

    using binding::types::Struct;
    Struct<Foo> *st = new Struct<Foo>("test::Foo", sizeof(Foo));
    BIND_STRUCT_METHOD(st, add, MARGS("a", "b"));
    BIND_STRUCT_METHOD(st, sum, MARGS());
    BIND_STRUCT_METHOD(st, bump, MARGS("n"));
    return st;
  }
};

/* Redefines `add` and adds `twice`; inherits `sum` and `bump` from Foo. */
struct Bar : Foo {
  int add(int a, int b)
  {
    return a + b + 1;
  }
  int twice(int a)
  {
    return 2 * a;
  }

  static binding::types::Struct<Bar> *defineBindings()
  {
    alloc::PermanentGuard guard;
    using binding::types::Struct;
    Struct<Bar> *st = new Struct<Bar>("test::Bar", sizeof(Bar));
    BIND_STRUCT_METHOD(st, add, MARGS("a", "b"));
    BIND_STRUCT_METHOD(st, twice, MARGS("a"));
    return st;
  }
};

static void test_inherit()
{
  auto *parent = Foo::defineBindings();
  auto *child = Bar::defineBindings();
  child->inherit(parent);

  const char *expected[] = {"add", "twice", "sum", "bump"};
  test_assert(child->methods.size() == 4);
  for (int i = 0; i < 4 && i < int(child->methods.size()); i++) {
    test_assert(strcmp(child->methods[i]->name.c_str(), expected[i]) == 0);
  }
  /* The child's own `add` wins over the parent's. */
  test_assert(child->methods[0] != parent->methods[0]);
}

int main()
{
  test_inherit();
  const auto *base = Foo::defineBindings();
  test_assert(base->type == BindingType::Struct);
  test_assert(base->methods.size() == 3);

  const types::Method *mAdd = base->methods[0];
  const types::Method *mSum = base->methods[1];
  const types::Method *mBump = base->methods[2];

  test_assert(strcmp(mAdd->name.c_str(), "add") == 0);
  test_assert(mAdd->type == BindingType::Method);
  test_assert(!mAdd->isConst);
  test_assert(mAdd->params.size() == 2);
  test_assert(mAdd->returnType != nullptr);
  test_assert(mAdd->returnType->type == BindingType::Boolean ||
              mAdd->returnType->type == BindingType::Number);

  test_assert(strcmp(mSum->name.c_str(), "sum") == 0);
  test_assert(mSum->isConst);
  test_assert(mSum->params.size() == 0);

  test_assert(strcmp(mBump->name.c_str(), "bump") == 0);
  test_assert(mBump->returnType == nullptr);
  test_assert(mBump->params.size() == 1);

  Foo f;

  {
    int a = 3, b = 4, ret = 0;
    void *args[] = {&a, &b};
    mAdd->thunk(&f, args, &ret);
    test_assert(ret == 7);
    test_assert(f.counter == 1);
  }

  {
    int n = 5;
    void *args[] = {&n};
    mBump->thunk(&f, args, nullptr);
    test_assert(f.counter == 6);
  }

  {
    int ret = 0;
    mSum->thunk(&f, nullptr, &ret);
    test_assert(ret == 6);
  }

  return test_end();
}
