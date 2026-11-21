#include "fixture.h"

#include "litestl/binding/foreign.h"

#include <new>

#include <cmath>
#include <cstdlib>
#include <cstring>

const litestl::binding::BindingBase *litestl::binding::Binder<fixture::Color>::bind()
{
  static types::Enum *e = nullptr;
  if (!e) {
    alloc::PermanentGuard guard;
    e = new types::Enum("fixture::Color", sizeof(fixture::Color));
    e->addItem("Red", fixture::Color::Red);
    e->addItem("Green", fixture::Color::Green);
    e->addItem("Blue", fixture::Color::Blue);
  }
  return e;
}

namespace fixture {
using namespace litestl::binding;

float Vec2::length() const
{
  return std::sqrt(x * x + y * y);
}
Vec2 Vec2::scaled(float s) const
{
  return Vec2(x * s, y * s);
}
void Vec2::add(const Vec2 &b)
{
  x += b.x;
  y += b.y;
}
Vec2 Vec2::make(float x, float y)
{
  return Vec2(x, y);
}

types::Struct<Vec2> *Vec2::defineBindings()
{
  static types::Struct<Vec2> *st = nullptr;
  if (st) {
    return st;
  }
  alloc::PermanentGuard guard;
  st = new types::Struct<Vec2>("fixture::Vec2", sizeof(Vec2));
  BIND_STRUCT_MEMBER(st, x);
  BIND_STRUCT_MEMBER(st, y);
  BIND_STRUCT_DEFAULT_CONSTRUCTOR(st);
  BIND_STRUCT_CONSTRUCTOR(st, "xy", float, float);
  BIND_STRUCT_COPY_CONSTRUCTOR(st);
  BIND_STRUCT_METHOD(st, length, MARGS());
  BIND_STRUCT_METHOD(st, scaled, MARGS("s"));
  BIND_STRUCT_METHOD(st, add, MARGS("b"));
  BIND_STRUCT_STATIC_METHOD(st, make, MARGS("x", "y"));
  BIND_STRUCT_METHOD_TEMPLATE(st, dotAs, TNAMES("T"), MARGS("b"), float, double);
  return st;
}

int Shape::live = 0;

Shape::Shape()
{
  live++;
}
Shape::Shape(int32_t id, Vec2 origin) : id(id), origin(origin)
{
  live++;
}
Shape::Shape(const Shape &b)
    : id(b.id), weight(b.weight), visible(b.visible), color(b.color), origin(b.origin),
      parent(b.parent), label(b.label), samples(b.samples)
{
  memcpy(corners, b.corners, sizeof(corners));
  live++;
}
Shape::~Shape()
{
  live--;
}
int32_t Shape::countSamples() const
{
  return int32_t(samples.size());
}
void Shape::addSample(float v)
{
  samples.append(v);
}
const char *Shape::describe() const
{
  return visible ? "visible shape" : "hidden shape";
}

types::Struct<Shape> *Shape::defineBindings()
{
  static types::Struct<Shape> *st = nullptr;
  if (st) {
    return st;
  }
  alloc::PermanentGuard guard;
  st = new types::Struct<Shape>("fixture::Shape", sizeof(Shape));
  BIND_STRUCT_MEMBER(st, id);
  BIND_STRUCT_MEMBER(st, weight);
  BIND_STRUCT_MEMBER(st, visible);
  BIND_STRUCT_MEMBER(st, color);
  BIND_STRUCT_MEMBER(st, origin);
  BIND_STRUCT_MEMBER(st, corners);
  BIND_STRUCT_MEMBER(st, parent);
  BIND_STRUCT_MEMBER(st, label);
  BIND_STRUCT_MEMBER(st, samples);
  BIND_STRUCT_DEFAULT_CONSTRUCTOR(st);
  BIND_STRUCT_CONSTRUCTOR(st, "main", int32_t, Vec2);
  BIND_STRUCT_COPY_CONSTRUCTOR(st);
  BIND_STRUCT_METHOD(st, countSamples, MARGS());
  BIND_STRUCT_METHOD(st, addSample, MARGS("v"));
  BIND_STRUCT_METHOD(st, describe, MARGS());
  return st;
}

int32_t addInts(int32_t a, int32_t b)
{
  return a + b;
}
double scaleD(double v, double s)
{
  return v * s;
}
bool isEven(int32_t v)
{
  return v % 2 == 0;
}
int64_t bigProduct(int64_t a, int64_t b)
{
  return a * b;
}
Color nextColor(Color c)
{
  return Color((int32_t(c) + 1) % 3);
}
float vecLength(const Vec2 &v)
{
  return v.length();
}
Vec2 midpoint(const Vec2 &a, const Vec2 &b)
{
  return Vec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
}
void moveBy(Vec2 *v, float dx, float dy)
{
  v->x += dx;
  v->y += dy;
}
Shape *makeShape(int32_t id)
{
  return alloc::New<Shape>("fixture::Shape", id, Vec2(1.0f, 2.0f));
}
Shape *sharedShape()
{
  static Shape *shared = nullptr;
  if (!shared) {
    alloc::PermanentGuard guard;
    shared = alloc::New<Shape>("fixture::sharedShape", 99, Vec2(0.0f, 0.0f));
    // The shared shape lives for the process; keep it out of the live count.
    Shape::live--;
  }
  return shared;
}
Shape *maybeShape(bool give)
{
  return give ? sharedShape() : nullptr;
}
int32_t liveShapes()
{
  return Shape::live;
}
const char *colorName(Color c)
{
  switch (c) {
  case Color::Red:
    return "red";
  case Color::Green:
    return "green";
  case Color::Blue:
    return "blue";
  }
  return "?";
}
int32_t textLength(const char *s)
{
  return int32_t(strlen(s));
}
util::string greet(const util::string &name)
{
  return util::string("hello, ") + name;
}
float sumFloats(std::span<const float> v)
{
  float sum = 0.0f;
  for (float f : v) {
    sum += f;
  }
  return sum;
}
void fillSquares(std::span<int32_t> out)
{
  for (size_t i = 0; i < out.size(); i++) {
    out[i] = int32_t(i * i);
  }
}
util::Vector<int32_t> range(int32_t n)
{
  util::Vector<int32_t> r;
  for (int32_t i = 0; i < n; i++) {
    r.append(i);
  }
  return r;
}
util::Vector<uint8_t> bytes(int32_t n)
{
  util::Vector<uint8_t> r;
  for (int32_t i = 0; i < n; i++) {
    r.append(uint8_t(i * 3));
  }
  return r;
}
float sumVector(const util::Vector<float> &v)
{
  float sum = 0.0f;
  for (float f : v) {
    sum += f;
  }
  return sum;
}
int32_t overloaded(int32_t v)
{
  return v * 2;
}
float overloaded(float v)
{
  return v * 0.5f;
}

/* ---- Probe: foreign dispatch ---- */

float Probe::scale(float) const
{
  return 0.0f;
}
int64_t Probe::big(int64_t) const
{
  return 0;
}
Color Probe::pick(Color c) const
{
  return c;
}
util::string Probe::echo(const util::string &s) const
{
  return s;
}
float Probe::lenOf(const Vec2 &) const
{
  return 0.0f;
}
Vec2 Probe::moved(Vec2 v, float) const
{
  return v;
}
bool Probe::touch(Vec2 *) const
{
  return false;
}
util::Vector<uint8_t> Probe::blob(const util::Vector<uint8_t> &b) const
{
  return b;
}
int32_t Probe::extra(int32_t v) const
{
  return v;
}
void Probe::keep(const Vec2 &) const
{
}
int32_t Probe::shapeId(const Shape &s) const
{
  return s.id;
}

types::Struct<Probe> *Probe::defineBindings()
{
  static types::Struct<Probe> *st = nullptr;
  if (st) {
    return st;
  }
  alloc::PermanentGuard guard;
  st = new types::Struct<Probe>("fixture::Probe", sizeof(Probe));
  st->isInterface = true;
  BIND_STRUCT_METHOD(st, scale, MARGS("x"));
  BIND_STRUCT_METHOD(st, big, MARGS("v"));
  BIND_STRUCT_METHOD(st, pick, MARGS("c"));
  BIND_STRUCT_METHOD(st, echo, MARGS("s"));
  BIND_STRUCT_METHOD(st, lenOf, MARGS("v"));
  BIND_STRUCT_METHOD(st, moved, MARGS("v", "dx"));
  BIND_STRUCT_METHOD(st, touch, MARGS("v")).argIsNullable("v");
  BIND_STRUCT_METHOD(st, blob, MARGS("b"));
  BIND_OPTIONAL_METHOD(st, extra, MARGS("v"));
  BIND_STRUCT_METHOD(st, keep, MARGS("v"));
  BIND_STRUCT_METHOD(st, shapeId, MARGS("s"));
  return st;
}

namespace {

util::string g_probeError;
bool g_probeFailed = false;

/* An interface forwarder in miniature: args[i] points at each argument (at the pointer,
 * for pointer parameters), and the return is constructed in `buf`. */
template <typename R, typename... A>
R probeCall(uint32_t h, const char *slot, R fallback, A... a)
{
  const types::Method *m = Probe::defineBindings()->findMethod(slot);
  void *args[sizeof...(A) + 1] = {static_cast<void *>(&a)...};
  g_probeError = util::string();
  alignas(R) unsigned char buf[sizeof(R)];
  g_probeFailed = !binding::foreign::call(h, m, args, buf, &g_probeError);
  if (g_probeFailed) {
    return fallback;
  }
  R *r = std::launder(reinterpret_cast<R *>(buf));
  R out(std::move(*r));
  r->~R();
  return out;
}

} // namespace

float probeScale(uint32_t h, float x)
{
  return probeCall(h, "scale", 0.0f, x);
}
int64_t probeBig(uint32_t h, int64_t v)
{
  return probeCall(h, "big", int64_t(0), v);
}
Color probePick(uint32_t h, Color c)
{
  return probeCall(h, "pick", Color::Red, c);
}
util::string probeEcho(uint32_t h, const util::string &s)
{
  return probeCall(h, "echo", util::string(), s);
}
float probeLenOf(uint32_t h, const Vec2 &v)
{
  return probeCall(h, "lenOf", 0.0f, v);
}
Vec2 probeMoved(uint32_t h, const Vec2 &v, float dx)
{
  return probeCall(h, "moved", Vec2(), v, dx);
}
bool probeTouch(uint32_t h, Vec2 *v)
{
  return probeCall(h, "touch", false, v);
}
util::Vector<uint8_t> probeBlob(uint32_t h, const util::Vector<uint8_t> &b)
{
  return probeCall(h, "blob", util::Vector<uint8_t>(), b);
}
int32_t probeExtra(uint32_t h, int32_t v)
{
  return probeCall(h, "extra", int32_t(-1), v);
}
void probeKeep(uint32_t h)
{
  Vec2 v(3.0f, 4.0f);
  void *args[1] = {&v};
  g_probeError = util::string();
  g_probeFailed = !binding::foreign::call(
      h, Probe::defineBindings()->findMethod("keep"), args, nullptr, &g_probeError);
}
int32_t probeShapeId(uint32_t h, const Shape &s)
{
  const types::Method *m = Probe::defineBindings()->findMethod("shapeId");
  void *args[1] = {const_cast<Shape *>(&s)};
  int32_t out = -1;
  g_probeError = util::string();
  g_probeFailed = !binding::foreign::call(h, m, args, &out, &g_probeError);
  return g_probeFailed ? -1 : out;
}
void probeTrap()
{
#if defined(__clang__)
  __builtin_trap();
#else
  abort();
#endif
}
bool probeFailed()
{
  return g_probeFailed;
}
util::string probeLastError()
{
  return g_probeError;
}
void probeRelease(uint32_t h)
{
  binding::foreign::release(h);
}

void registerFixture(BindingManager *mgr)
{
  alloc::PermanentGuard guard;

  mgr->add(Bind<Color>());

  mgr->add(Bind<Vec2>());
  mgr->add(Bind<Shape>());
  mgr->add(Bind<Box<float>>());
  mgr->add(Bind<Box<double>>());

  mgr->add(Bind<Probe>());
  BIND_FUNCTION(mgr, "fixture::probeScale", probeScale, MARGS("h", "x"));
  BIND_FUNCTION(mgr, "fixture::probeBig", probeBig, MARGS("h", "v"));
  BIND_FUNCTION(mgr, "fixture::probePick", probePick, MARGS("h", "c"));
  BIND_FUNCTION(mgr, "fixture::probeEcho", probeEcho, MARGS("h", "s"));
  BIND_FUNCTION(mgr, "fixture::probeLenOf", probeLenOf, MARGS("h", "v"));
  BIND_FUNCTION(mgr, "fixture::probeMoved", probeMoved, MARGS("h", "v", "dx"));
  BIND_FUNCTION(mgr, "fixture::probeTouch", probeTouch, MARGS("h", "v"))
      .argIsNullable("v");
  BIND_FUNCTION(mgr, "fixture::probeBlob", probeBlob, MARGS("h", "b"));
  BIND_FUNCTION(mgr, "fixture::probeExtra", probeExtra, MARGS("h", "v"));
  BIND_FUNCTION(mgr, "fixture::probeKeep", probeKeep, MARGS("h"));
  BIND_FUNCTION(mgr, "fixture::probeShapeId", probeShapeId, MARGS("h", "s"));
  BIND_FUNCTION(mgr, "fixture::probeTrap", probeTrap, MARGS());
  BIND_FUNCTION(mgr, "fixture::probeFailed", probeFailed, MARGS());
  BIND_FUNCTION(mgr, "fixture::probeLastError", probeLastError, MARGS());
  BIND_FUNCTION(mgr, "fixture::probeRelease", probeRelease, MARGS("h"));

  BIND_FUNCTION(mgr, "fixture::addInts", addInts, MARGS("a", "b"));
  BIND_FUNCTION(mgr, "fixture::scaleD", scaleD, MARGS("v", "s"));
  BIND_FUNCTION(mgr, "fixture::isEven", isEven, MARGS("v"));
  BIND_FUNCTION(mgr, "fixture::bigProduct", bigProduct, MARGS("a", "b"));
  BIND_FUNCTION(mgr, "fixture::nextColor", nextColor, MARGS("c"));
  BIND_FUNCTION(mgr, "fixture::vecLength", vecLength, MARGS("v"));
  BIND_FUNCTION(mgr, "fixture::midpoint", midpoint, MARGS("a", "b"));
  BIND_FUNCTION(mgr, "fixture::moveBy", moveBy, MARGS("v", "dx", "dy"));
  BIND_FUNCTION(mgr, "fixture::makeShape", makeShape, MARGS("id")).returnsOwned();
  BIND_FUNCTION(mgr, "fixture::sharedShape", sharedShape, MARGS());
  BIND_FUNCTION(mgr, "fixture::maybeShape", maybeShape, MARGS("give"));
  BIND_FUNCTION(mgr, "fixture::liveShapes", liveShapes, MARGS());
  BIND_FUNCTION(mgr, "fixture::colorName", colorName, MARGS("c"));
  BIND_FUNCTION(mgr, "fixture::textLength", textLength, MARGS("s"));
  BIND_FUNCTION(mgr, "fixture::greet", greet, MARGS("name"));
  BIND_FUNCTION(mgr, "fixture::sumFloats", sumFloats, MARGS("v"));
  BIND_FUNCTION(mgr, "fixture::fillSquares", fillSquares, MARGS("out"));
  BIND_FUNCTION(mgr, "fixture::range", range, MARGS("n"));
  BIND_FUNCTION(mgr, "fixture::bytes", bytes, MARGS("n"));
  BIND_FUNCTION(mgr, "fixture::sumVector", sumVector, MARGS("v"));
  BIND_FUNCTION_SIG(mgr, "fixture::overloaded", overloaded, int32_t, (int32_t), MARGS("v"));
  BIND_FUNCTION_SIG(mgr, "fixture::overloaded", overloaded, float, (float), MARGS("v"))
      .setJsName("overloadedF");

  BIND_FUNCTION_TEMPLATE(
      mgr, "fixture::makeBox", fixture::makeBox, TNAMES("T"), MARGS("lo", "hi"), float, double);
  BIND_FUNCTION_TEMPLATE(
      mgr, "fixture::boxExtent", fixture::boxExtent, TNAMES("T"), MARGS("b"), float, double);
  BIND_FUNCTION_TEMPLATE(
      mgr, "fixture::sumSpan", fixture::sumSpan, TNAMES("T"), MARGS("v"), float, double);
  // makeBox returns owned boxes; flag every instantiation.
  for (const types::Method *m : mgr->getFunctions()) {
    if (strcmp(m->name.c_str(), "fixture::makeBox") == 0) {
      const_cast<types::Method *>(m)->returnsOwned();
    }
  }

  mgr->validate();
}

BindingManager *fixtureManager()
{
  static BindingManager *mgr = nullptr;
  if (!mgr) {
    alloc::PermanentGuard guard;
    mgr = alloc::New<BindingManager>("fixture BindingManager");
    registerFixture(mgr);
  }
  return mgr;
}

} // namespace fixture
