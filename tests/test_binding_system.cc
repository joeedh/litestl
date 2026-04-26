#include "binding/binding.h"
#include "binding/manager.h"
#include "math/vector.h"
#include "util/vector.h"

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <span>

using namespace litestl;
using namespace litestl::binding;
using namespace litestl::util;
using namespace litestl::math;

static Vector<char> msgBuf = {0};
extern "C" const char *GetMessageBuf() {
  return msgBuf.size() == 0 ? nullptr : msgBuf.data();
}
extern "C" void ClearMessageBuf() {
  msgBuf.clear();
  msgBuf.append(0);
}

void testPrint(const char *fmt, ...) {
  char buf[512];

  va_list args;
  va_start(args, fmt);

  int count = vsnprintf(buf, sizeof(buf), fmt, args);
  count = std::min(count, int(sizeof(buf)) - 1);

  std::span<char> s = {(char*)buf, size_t(count)};
  
  msgBuf.pop_back();
  msgBuf.concat(s);
  msgBuf.append(0);

  va_end(args);
}

static void test_assert_msg(bool expr, const char *str)
{
  if (!expr) {
    fprintf(stderr, "%s failed\n", str);
    fflush(stderr);
    exit(-1);
  }
}
#define test_assert(expr) test_assert_msg(expr, #expr)

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
    using binding::types::Constructor;
    using binding::types::Struct;
    Struct<Foo> *st = new Struct<Foo>("test::Foo", sizeof(Foo));

    BIND_STRUCT_DEFAULT_CONSTRUCTOR(st);

    BIND_STRUCT_METHOD(st, add);
    BIND_STRUCT_METHOD(st, sum);
    BIND_STRUCT_METHOD(st, bump);
    return st;
  }
};

struct VecTest {
  Vector<float3> pos;
  Vector<float> f;
  int flag;
  string str = "test";

  VecTest()
  {
    testPrint("constructing VecTest\n");

    pos.append(float3(1.0f, 2.0f, 3.0f));
    f.append(1.0f);
    f.append(2.0f);
    f.append(3.0f);
    f.append(4.0f);
    flag = 42;
    str = "hello";
  }

  void print() const {
    testPrint("pos: (%.2f, %.2f, %.2f)\n", pos[0][0], pos[0][1], pos[0][2]);
    testPrint("f: (");
    for (float v : f) {
      testPrint("%.2f, ", v);
    }
    testPrint(")\n");
    testPrint("flag: %d\n", flag);
    testPrint("str: %s\n", str.c_str());
    testPrint("\n");
  }

  VecTest(int a, double b, float c, bool d, bool e) {
    testPrint("a: %d, b: %.2lf, c: %.2f, d: %s, e: %s\n", a, b, c, d ? "true" : "false", e ? "true" : "false");
  }

  static binding::types::Struct<VecTest> *defineBindings()
  {
    using binding::types::Constructor;
    using binding::types::Struct;

    Struct<VecTest> *st = new Struct<VecTest>("test::VecTest", sizeof(VecTest));
    BIND_STRUCT_DEFAULT_CONSTRUCTOR(st);
    BIND_STRUCT_CONSTRUCTOR(st, "main", int, double, float, bool, bool);

    BIND_STRUCT_METHOD(st, print);

    BIND_STRUCT_MEMBER(st, pos);
    BIND_STRUCT_MEMBER(st, f);
    BIND_STRUCT_MEMBER(st, flag);
    BIND_STRUCT_MEMBER(st, str);

    return st;
  }
};

extern "C" BindingManager *getBindingManager()
{
  // tell allocator the binding system's allocations are not leaks
  alloc::PermanentGuard guard;

  using namespace litestl::binding;
  using namespace litestl;
  BindingManager *manager = alloc::New<BindingManager>("BindingManager");

  manager->add(Bind<Foo>());
  manager->add(Bind<VecTest>());

  return manager;
}

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

  return 0;
}
