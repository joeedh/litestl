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

int main()
{
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
