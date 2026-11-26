/*
 * Descriptors for free functions, static methods and template instantiations,
 * invoked through their thunks; plus the function registry's checks.
 */

#include "bindings/fixture.h"
#include "../binding/binding_dispatch.h"
#include "../binding/generators/typescript.h"
#include "test_util.h"

#include <cstring>

test_init;

using namespace litestl;
using namespace litestl::binding;

static const types::Method *findFunction(BindingManager *mgr, const char *fullName)
{
  for (const types::Method *m : mgr->getFunctions()) {
    if (strcmp(m->buildFullName().c_str(), fullName) == 0) {
      return m;
    }
  }
  fprintf(stderr, "function not found: %s\n", fullName);
  return nullptr;
}

static util::Vector<const types::Method *> findGroup(BindingManager *mgr, const char *jsName)
{
  for (const auto &group : mgr->functionGroups()) {
    if (strcmp(group[0]->qualifiedJsName().c_str(), jsName) == 0) {
      return group;
    }
  }
  return {};
}

static const types::Method *findMethod(const types::_StructBase *st,
                                       const char *name,
                                       int index = 0)
{
  for (const types::Method *m : st->methods) {
    if (strcmp(m->name.c_str(), name) == 0 && index-- == 0) {
      return m;
    }
  }
  return nullptr;
}

static void testRegistry(BindingManager *mgr)
{
  test_assert(mgr->checkFunctions());

  const types::Method *add = findFunction(mgr, "fixture::addInts[int32,int32]");
  test_assert(add && add->isStatic);
  test_assert(add && strcmp(add->getJsName().c_str(), "addInts") == 0);
  test_assert(add && strcmp(add->namespaceName().c_str(), "fixture") == 0);
  test_assert(mgr->getBindings().contains("fixture::addInts[int32,int32]"));

  test_assert(findGroup(mgr, "fixture::makeBox").size() == 2);
  test_assert(findGroup(mgr, "fixture::overloaded").size() == 1);
  test_assert(findGroup(mgr, "fixture::overloadedF").size() == 1);
  test_assert(findFunction(mgr, "fixture::makeBox<float>[float,float]") != nullptr);
  test_assert(findFunction(mgr, "fixture::overloaded[float]") != nullptr);

  // Plain overloads sharing a JS name are rejected until one is renamed.
  BindingManager bad;
  BIND_FUNCTION_SIG(&bad, "x::f", fixture::overloaded, int32_t, (int32_t), MARGS("v"));
  BIND_FUNCTION_SIG(&bad, "x::f", fixture::overloaded, float, (float), MARGS("v"));
  string error;
  test_assert(!bad.checkFunctions(&error));
  test_assert(strstr(error.c_str(), "x::f") != nullptr);
}

static void testThunks(BindingManager *mgr)
{
  {
    const types::Method *m = findFunction(mgr, "fixture::addInts[int32,int32]");
    int32_t a = 3, b = 4, ret = 0;
    void *args[] = {&a, &b};
    m->thunk(nullptr, args, &ret);
    test_assert(ret == 7);
  }
  {
    // const util::string& binds as a plain string.
    const types::Method *m =
        findFunction(mgr, "fixture::greet[litestl::util::String]");
    test_assert(m && m->params[0].type->type == BindingType::Struct);
    util::string name("bob");
    alignas(util::string) char ret[sizeof(util::string)];
    void *args[] = {&name};
    m->thunk(nullptr, args, ret);
    util::string *out = reinterpret_cast<util::string *>(ret);
    test_assert(strcmp(out->c_str(), "hello, bob") == 0);
    std::destroy_at(out);
  }
  {
    const types::Method *m =
        findFunction(mgr, "fixture::sumFloats[litestl::util::Span<float,true>]");
    test_assert(m != nullptr);
    float data[] = {1.0f, 2.0f, 3.5f};
    std::span<const float> sp(data, 3);
    float ret = 0.0f;
    void *args[] = {&sp};
    m->thunk(nullptr, args, &ret);
    test_assert(ret == 6.5f);
  }
  {
    const types::Method *m = findFunction(mgr, "fixture::colorName[fixture::Color]");
    test_assert(m && m->returnType->type == BindingType::Struct);
    test_assert(m && strcmp(m->returnType->name.c_str(), "litestl::util::CString") == 0);
    fixture::Color c = fixture::Color::Blue;
    const char *ret = nullptr;
    void *args[] = {&c};
    m->thunk(nullptr, args, &ret);
    test_assert(ret && strcmp(ret, "blue") == 0);
  }
  {
    const types::Method *m = findFunction(mgr, "fixture::makeShape[int32]");
    test_assert(m && m->returnPolicy == types::ReturnPolicy::Owned);
    test_assert(m && m->returnType->type == BindingType::Pointer);
    int32_t id = 5;
    fixture::Shape *ret = nullptr;
    void *args[] = {&id};
    int before = fixture::liveShapes();
    m->thunk(nullptr, args, &ret);
    test_assert(ret && ret->id == 5);
    test_assert(fixture::liveShapes() == before + 1);
    alloc::Delete(ret);
    test_assert(fixture::liveShapes() == before);
  }
  {
    // const Vec2& stays a reference: args[i] is the object's address.
    const types::Method *m = findFunction(mgr, "fixture::vecLength[fixture::Vec2&]");
    test_assert(m && m->params[0].type->type == BindingType::Reference);
    fixture::Vec2 v(3.0f, 4.0f);
    float ret = 0.0f;
    void *args[] = {&v};
    m->thunk(nullptr, args, &ret);
    test_assert(ret == 5.0f);
  }
  {
    const types::Method *m = findFunction(mgr, "fixture::moveBy[fixture::Vec2*,float,float]");
    test_assert(m && m->params[0].type->type == BindingType::Pointer);
    test_assert(m && static_cast<const types::Pointer *>(m->params[0].type)->isNonNull);
    fixture::Vec2 v(1.0f, 1.0f);
    fixture::Vec2 *vp = &v;
    float dx = 2.0f, dy = 3.0f;
    void *args[] = {&vp, &dx, &dy};
    m->thunk(nullptr, args, nullptr);
    test_assert(v.x == 3.0f && v.y == 4.0f);
  }
}

static void testStructCallables()
{
  const types::_StructBase *vec2 =
      static_cast<const types::_StructBase *>(Bind<fixture::Vec2>());

  const types::Method *make = findMethod(vec2, "make");
  test_assert(make && make->isStatic);
  {
    float x = 1.0f, y = 2.0f;
    alignas(fixture::Vec2) char ret[sizeof(fixture::Vec2)];
    void *args[] = {&x, &y};
    make->thunk(nullptr, args, ret);
    fixture::Vec2 *v = reinterpret_cast<fixture::Vec2 *>(ret);
    test_assert(v->x == 1.0f && v->y == 2.0f);
  }

  const types::Method *dotF = findMethod(vec2, "dotAs", 0);
  const types::Method *dotD = findMethod(vec2, "dotAs", 1);
  test_assert(dotF && dotD && !dotF->isStatic);
  test_assert(strcmp(dotF->buildFullName().c_str(), "dotAs<float>[fixture::Vec2&]") == 0);
  test_assert(strcmp(dotD->buildFullName().c_str(), "dotAs<double>[fixture::Vec2&]") == 0);
  {
    fixture::Vec2 a(1.0f, 2.0f), b(3.0f, 4.0f);
    double ret = 0.0;
    void *args[] = {&b};
    dotD->thunk(&a, args, &ret);
    test_assert(ret == 11.0);
  }

  const types::_StructBase *boxF =
      static_cast<const types::_StructBase *>(Bind<fixture::Box<float>>());
  const types::Method *unit = findMethod(boxF, "unit");
  test_assert(unit && unit->isStatic);
}

static void testDispatch(BindingManager *mgr)
{
  using dispatch::Mode;
  test_assert(dispatch::mode(findGroup(mgr, "fixture::addInts")) == Mode::Single);
  test_assert(dispatch::mode(findGroup(mgr, "fixture::makeBox")) == Mode::Select);
  test_assert(dispatch::mode(findGroup(mgr, "fixture::boxExtent")) == Mode::Infer);
  // Spans arrive as plain arrays, so they can't drive inference.
  test_assert(dispatch::mode(findGroup(mgr, "fixture::sumSpan")) == Mode::Select);

  const types::Method *m = findFunction(mgr, "fixture::makeBox<double>[double,double]");
  test_assert(m && strcmp(dispatch::selectorKey(m).c_str(), "double") == 0);
}

static util::Map<string, string> *generate(const BindingManager *mgr)
{
  util::Vector<const BindingBase *> types;
  for (auto *type : mgr->getBindings().values()) {
    types.append(type);
  }
  return generators::generateTypescript(types);
}

static bool fileHas(util::Map<string, string> *files, const char *file, const char *text)
{
  const string *body = files->lookup_ptr(file);
  if (!body) {
    fprintf(stderr, "generated file missing: %s\n", file);
    return false;
  }
  if (!strstr(body->c_str(), text)) {
    fprintf(stderr, "%s lacks:\n  %s\n--- %s ---\n%s\n", file, text, file, body->c_str());
    return false;
  }
  return true;
}

static void testGenerator(BindingManager *mgr)
{
  auto *files = generate(mgr);

  // Inferred from Box<float>/Box<double> arguments.
  test_assert(fileHas(files,
                      "fixture/functions.ts",
                      "export const boxExtent = bindFunction(\"fixture::boxExtent\") as "
                      "<T extends (float | double)>(b: Box<T>) => T;"));
  // No struct argument to infer from: selector key first.
  test_assert(fileHas(files, "fixture/functions.ts", "\"float\": [float],"));
  test_assert(fileHas(files,
                      "fixture/functions.ts",
                      "<K extends keyof makeBox_Args>(_t: K, lo: makeBox_Args[K][0], hi: "
                      "makeBox_Args[K][0]) => Box<makeBox_Args[K][0]> | undefined;"));
  test_assert(fileHas(files,
                      "fixture/functions.ts",
                      "export const overloadedF = bindFunction(\"fixture::overloadedF\") as "
                      "(v: float) => float;"));
  test_assert(fileHas(files, "fixture/functions.ts", "(n: int32) => Uint8Array;"));
  test_assert(fileHas(files, "fixture/functions.ts", "(v: ArrayLike<float>) => float;"));
  test_assert(fileHas(files, "fixture/functions.ts", "(name: string) => string;"));

  // An interface struct also gets the shape a JS implementation has (foreign dispatch).
  test_assert(fileHas(files, "fixture/Probe.ts", "export interface ProbeImpl {"));
  test_assert(fileHas(files, "fixture/Probe.ts", "  extra?(v: int32): int32\n"));
  test_assert(fileHas(files, "fixture/Probe.ts", "  touch(v: Vec2 | null): boolean\n"));
  test_assert(
      fileHas(files, "fixture/Probe.ts", "  blob(b: Uint8Array): ArrayLike<uint8>\n"));
  test_assert(!fileHas(files, "fixture/Vec2.ts", "Vec2Impl"));

  // Template struct members and constructors, inferred across Box<float>/Box<double>.
  test_assert(fileHas(files, "fixture/Box.ts", "export interface Box<T> {"));
  test_assert(fileHas(files, "fixture/Box.ts", "  lo: T\n"));
  test_assert(fileHas(files, "fixture/Box.ts", "  extent(): T\n"));
  test_assert(fileHas(files, "fixture/Box.ts", "  new(b: Box<T>): Box<T>\n"));
  test_assert(fileHas(files,
                      "fixture/Box.ts",
                      "unit: bindStatic(\"fixture::Box\", \"unit\") as <K extends keyof "
                      "Box_unit_Args>(_t: K) => Box<Box_unit_Args[K][0]>,"));

  test_assert(fileHas(files,
                      "fixture/Vec2.ts",
                      "  dotAs<K extends keyof Vec2_dotAs_Args>(_t: K, b: Vec2): "
                      "Vec2_dotAs_Args[K][0]\n"));
  test_assert(fileHas(files,
                      "fixture/Vec2.ts",
                      "make: bindStatic(\"fixture::Vec2\", \"make\") as (x: float, y: float) => "
                      "Vec2,"));

  // Vector's ptrWithCount(T*, int) no longer needs a hand-written marker.
  test_assert(fileHas(files, "litestl/util/Vector.ts", "new(arg0: T, arg1: int32)"));
  alloc::Delete(files);

  // One instantiation proves nothing about T: render concretely.
  BindingManager single;
  single.add(Bind<fixture::Box<float>>());
  files = generate(&single);
  test_assert(fileHas(files, "fixture/Box.ts", "  lo: float\n"));
  alloc::Delete(files);
}

int main()
{
  BindingManager *mgr = fixture::fixtureManager();

  testRegistry(mgr);
  testThunks(mgr);
  testStructCallables();
  testDispatch(mgr);
  testGenerator(mgr);

  return test_end();
}
