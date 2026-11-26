/* Descriptor fields used by coredcc's interfaces: Struct::isInterface and typeKey,
 * findMethod, Method::isOptional, Pointer/Reference::isConst, noexcept methods and
 * adaptor methods. */
#include "../binding/binding.h"
#include "test_util.h"

#include <cstring>

test_init;

using namespace litestl;
using namespace litestl::binding;

struct Ray {
  float t = 0.0f;

  static types::Struct<Ray> *defineBindings()
  {
    alloc::PermanentGuard guard;
    static types::Struct<Ray> *st = nullptr;
    if (!st) {
      st = new types::Struct<Ray>("test::Ray", sizeof(Ray));
      BIND_STRUCT_MEMBER(st, t);
    }
    return st;
  }
};

/* A different C++ type bound under Ray's name: typeKey must still tell them apart. */
struct Impostor {
  float t = 0.0f;

  static types::Struct<Impostor> *defineBindings()
  {
    alloc::PermanentGuard guard;
    auto *st = new types::Struct<Impostor>("test::Ray", sizeof(Impostor));
    BIND_STRUCT_MEMBER(st, t);
    return st;
  }
};

struct Shape {
  int n = 3;

  int count() const noexcept
  {
    return n;
  }
  void grow(int by) noexcept
  {
    n += by;
  }
  double cost(int a, int b) const
  {
    return double(a + b);
  }
  void refs(const Ray &a, Ray &b, const Ray *c, Ray *d)
  {
    b.t = a.t + c->t + d->t;
  }
  const Ray *constRet() const
  {
    return nullptr;
  }
  void take(util::string &&s, const util::string &t)
  {
    n = int(s.size() + t.size());
  }

  static types::Struct<Shape> *defineBindings()
  {
    alloc::PermanentGuard guard;
    auto *st = new types::Struct<Shape>("test::Shape", sizeof(Shape));
    st->isInterface = true;
    BIND_STRUCT_METHOD(st, count, MARGS());
    BIND_STRUCT_METHOD(st, grow, MARGS("by"));
    /* Two names: MARGS expands to a type with a comma, which the macro must survive. */
    BIND_OPTIONAL_METHOD(st, cost, MARGS("a", "b"));
    BIND_STRUCT_METHOD(st, refs, MARGS("a", "b", "c", "d"));
    BIND_STRUCT_METHOD(st, constRet, MARGS());
    BIND_STRUCT_METHOD(st, take, MARGS("s", "t"));
    return st;
  }
};

/* A type with no bindings of its own, adapted by free functions. */
struct Foreign {
  int verts = 0;
};
static int foreignCount(const Foreign &f)
{
  return f.verts;
}
static void foreignAdd(Foreign &f, int n) noexcept
{
  f.verts += n;
}

/* clone() copies descriptors, which are never freed by design. */

static void test_type_key()
{
  alloc::PermanentGuard guard;
  const auto *ray = Ray::defineBindings();
  const auto *impostor = Impostor::defineBindings();
  test_assert(ray->typeKey != nullptr);
  test_assert(ray->typeKey == types::typeKeyOf<Ray>());
  test_assert(ray->typeKey == types::typeKeyOf<const Ray>());
  test_assert(impostor->typeKey == types::typeKeyOf<Impostor>());
  /* Same name, same size, different C++ type. */
  test_assert(ray->name == impostor->name);
  test_assert(ray->structSize == impostor->structSize);
  test_assert(ray->typeKey != impostor->typeKey);

  const auto *copy = static_cast<const types::_StructBase *>(ray->clone());
  test_assert(copy->typeKey == ray->typeKey);
}

static void test_interface_flags()
{
  alloc::PermanentGuard guard;
  const auto *shape = Shape::defineBindings();
  test_assert(shape->isInterface);
  test_assert(!Ray::defineBindings()->isInterface);
  const auto *copy = static_cast<const types::_StructBase *>(shape->clone());
  test_assert(copy->isInterface);

  const types::Method *count = shape->findMethod("count");
  const types::Method *cost = shape->findMethod("cost");
  test_assert(count != nullptr && cost != nullptr);
  test_assert(shape->findMethod("missing") == nullptr);
  test_assert(!count->isOptional);
  test_assert(cost->isOptional);
  test_assert(cost->params.size() == 2);
  test_assert(strcmp(cost->params[1].name.c_str(), "b") == 0);
  const auto *costCopy = static_cast<const types::Method *>(cost->clone());
  test_assert(costCopy->isOptional);
}

static void test_noexcept_methods()
{
  const auto *shape = Shape::defineBindings();
  const types::Method *count = shape->findMethod("count");
  const types::Method *grow = shape->findMethod("grow");
  test_assert(count->isConst);
  test_assert(!grow->isConst);

  Shape s;
  int by = 4;
  void *args[] = {&by};
  grow->thunk(&s, args, nullptr);
  int ret = 0;
  count->thunk(&s, nullptr, &ret);
  test_assert(ret == 7);
}

static void test_const_recording()
{
  alloc::PermanentGuard guard;
  const types::Method *refs = Shape::defineBindings()->findMethod("refs");
  test_assert(refs->params.size() == 4);
  const auto *a = refs->params[0].type;
  const auto *b = refs->params[1].type;
  const auto *c = refs->params[2].type;
  const auto *d = refs->params[3].type;
  test_assert(a->type == BindingType::Reference && b->type == BindingType::Reference);
  test_assert(c->type == BindingType::Pointer && d->type == BindingType::Pointer);
  test_assert(static_cast<const types::Reference *>(a)->isConst);
  test_assert(!static_cast<const types::Reference *>(b)->isConst);
  test_assert(static_cast<const types::Pointer *>(c)->isConst);
  test_assert(!static_cast<const types::Pointer *>(d)->isConst);
  /* Const-ness is a flag, not part of the full name (registration keys use the name). */
  test_assert(a->buildFullName() == b->buildFullName());

  const types::Method *constRet = Shape::defineBindings()->findMethod("constRet");
  test_assert(constRet->returnType->type == BindingType::Pointer);
  test_assert(static_cast<const types::Pointer *>(constRet->returnType)->isConst);

  const auto *copy = static_cast<const types::Reference *>(a->clone());
  test_assert(copy->isConst);

  /* string&& and const string& both bind as a plain string; only the flag differs. */
  const types::Method *take = Shape::defineBindings()->findMethod("take");
  test_assert(take->params[0].isRvalueRef);
  test_assert(!take->params[1].isRvalueRef);
  test_assert(!refs->params[0].isRvalueRef);
  test_assert(take->params[0].type->buildFullName() == take->params[1].type->buildFullName());
}

static void test_adaptor_methods()
{
  alloc::PermanentGuard guard;
  auto *st = new types::Struct<Foreign>("test::ForeignAdaptor", sizeof(Foreign));
  BIND_ADAPTOR_METHOD(st, count, foreignCount, MARGS());
  BIND_ADAPTOR_METHOD(st, add, foreignAdd, MARGS("n"));

  const types::Method *count = st->findMethod("count");
  const types::Method *add = st->findMethod("add");
  test_assert(count->isConst);
  test_assert(!add->isConst);
  test_assert(count->params.size() == 0);
  test_assert(add->params.size() == 1);
  test_assert(strcmp(add->params[0].name.c_str(), "n") == 0);

  Foreign f;
  int n = 5;
  void *args[] = {&n};
  add->thunk(&f, args, nullptr);
  int ret = 0;
  count->thunk(&f, nullptr, &ret);
  test_assert(ret == 5);
}

int main()
{
  test_type_key();
  test_interface_flags();
  test_noexcept_methods();
  test_const_recording();
  test_adaptor_methods();
  return test_end();
}
