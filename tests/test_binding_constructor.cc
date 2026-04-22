#include "binding/binding.h"
#include "test_util.h"

#include <cstdio>
#include <cstring>
#include <new>

test_init;

using namespace litestl;
using namespace litestl::binding;

struct Foo {
  int a;
  int b;

  Foo() : a(0), b(0)
  {
  }
  Foo(int x, int y) : a(x), b(y)
  {
  }

  static binding::types::Struct<Foo> *defineBindings()
  {
    alloc::PermanentGuard guard;

    using binding::types::Struct;
    Struct<Foo> *st = new Struct<Foo>("test::Foo", sizeof(Foo));
    BIND_STRUCT_DEFAULT_CONSTRUCTOR(st);
    BIND_STRUCT_CONSTRUCTOR(st, int, int);
    return st;
  }
};

int main()
{
  const auto *base = Foo::defineBindings();
  test_assert(base->type == BindingType::Struct);
  test_assert(base->constructors.size() == 2);

  const types::Constructor *cDefault = base->constructors[0];
  const types::Constructor *cTwoArg = base->constructors[1];

  test_assert(cDefault->type == BindingType::Constructor);
  test_assert(cDefault->ownerType == base);
  test_assert(cDefault->params.size() == 0);
  test_assert(cDefault->thunk != nullptr);

  test_assert(cTwoArg->params.size() == 2);
  test_assert(cTwoArg->params[0].type->type == BindingType::Number);
  test_assert(cTwoArg->params[1].type->type == BindingType::Number);

  {
    alignas(Foo) char buf[sizeof(Foo)];
    cDefault->thunk(buf, nullptr);
    Foo *f = reinterpret_cast<Foo *>(buf);
    test_assert(f->a == 0);
    test_assert(f->b == 0);
    f->~Foo();
  }

  {
    alignas(Foo) char buf[sizeof(Foo)];
    int x = 11, y = 22;
    void *args[] = {&x, &y};
    cTwoArg->thunk(buf, args);
    Foo *f = reinterpret_cast<Foo *>(buf);
    test_assert(f->a == 11);
    test_assert(f->b == 22);
    f->~Foo();
  }

  return test_end();
}
