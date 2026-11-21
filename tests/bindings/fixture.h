#pragma once

/*
 * Types and functions the binding conformance tests bind. One registration
 * (registerFixture) feeds the native generator test, the WASM module and the
 * N-API addon, so every backend is checked against the same descriptors.
 */

#include "litestl/binding/binding.h"
#include "litestl/binding/manager.h"
#include "litestl/util/string.h"
#include "litestl/util/vector.h"

#include <cstdint>
#include <span>

namespace fixture {
using namespace litestl;

enum class Color : int32_t { Red = 0, Green = 1, Blue = 2 };
} // namespace fixture

namespace litestl::binding {
template <> struct Binder<fixture::Color> {
  static const BindingBase *bind();
};
} // namespace litestl::binding

namespace fixture {

struct Vec2 {
  float x = 0.0f;
  float y = 0.0f;

  Vec2() = default;
  Vec2(float x, float y) : x(x), y(y) {}
  Vec2(const Vec2 &b) = default;

  float length() const;
  Vec2 scaled(float s) const;
  void add(const Vec2 &b);
  static Vec2 make(float x, float y);
  /** Instantiated for float and double: neither argument is a struct, so JS
   * picks the instantiation with a selector key. */
  template <typename T> T dotAs(const Vec2 &b) const
  {
    return T(x) * T(b.x) + T(y) * T(b.y);
  }

  static binding::types::Struct<Vec2> *defineBindings();
};

/** Counts live instances so the tests can see ownership being honoured. */
struct Shape {
  int32_t id = 0;
  double weight = 1.5;
  bool visible = true;
  Color color = Color::Green;
  Vec2 origin;
  float corners[4] = {1.0f, 2.0f, 3.0f, 4.0f};
  Shape *parent = nullptr;
  util::string label;
  util::Vector<float> samples;

  static int live;

  Shape();
  Shape(int32_t id, Vec2 origin);
  Shape(const Shape &b);
  ~Shape();

  int32_t countSamples() const;
  void addSample(float v);
  const char *describe() const;

  static binding::types::Struct<Shape> *defineBindings();
};

template <typename T> struct Box {
  T lo = T(0);
  T hi = T(1);

  T extent() const
  {
    return hi - lo;
  }
  static Box unit()
  {
    return Box();
  }

  static binding::types::Struct<Box> *defineBindings()
  {
    static binding::types::Struct<Box> *st = nullptr;
    if (st) {
      return st;
    }
    alloc::PermanentGuard guard;
    st = new binding::types::Struct<Box>("fixture::Box", sizeof(Box));
    st->addTemplateParam(binding::Bind<T>(), "T");
    BIND_STRUCT_MEMBER(st, lo);
    BIND_STRUCT_MEMBER(st, hi);
    BIND_STRUCT_DEFAULT_CONSTRUCTOR(st);
    BIND_STRUCT_COPY_CONSTRUCTOR(st);
    BIND_STRUCT_METHOD(st, extent, MARGS());
    BIND_STRUCT_STATIC_METHOD(st, unit, MARGS());
    return st;
  }
};

/**
 * An interface JS implements (foreign dispatch, binding/foreign.h). Its methods are
 * the slots' descriptors; C++ never runs them, it calls a JS object's through
 * foreign::call (the probe* functions below).
 */
struct Probe {
  float scale(float x) const;
  int64_t big(int64_t v) const;
  Color pick(Color c) const;
  util::string echo(const util::string &s) const;
  float lenOf(const Vec2 &v) const;
  Vec2 moved(Vec2 v, float dx) const;
  /** Writes through `v` when it isn't null; returns whether it was. */
  bool touch(Vec2 *v) const;
  util::Vector<uint8_t> blob(const util::Vector<uint8_t> &b) const;
  /** Left out by implementations that don't want it. */
  int32_t extra(int32_t v) const;
  void keep(const Vec2 &v) const;
  /** A struct argument with an embedded struct (origin). */
  int32_t shapeId(const Shape &s) const;

  static binding::types::Struct<Probe> *defineBindings();
};

/* Each calls the slot of the same name on host object `h`; on failure it returns a
 * default and probeLastError() says why. */
float probeScale(uint32_t h, float x);
int64_t probeBig(uint32_t h, int64_t v);
Color probePick(uint32_t h, Color c);
util::string probeEcho(uint32_t h, const util::string &s);
float probeLenOf(uint32_t h, const Vec2 &v);
Vec2 probeMoved(uint32_t h, const Vec2 &v, float dx);
bool probeTouch(uint32_t h, Vec2 *v);
util::Vector<uint8_t> probeBlob(uint32_t h, const util::Vector<uint8_t> &b);
int32_t probeExtra(uint32_t h, int32_t v);
/** Passes a stack Vec2(3, 4) to `keep`. */
void probeKeep(uint32_t h);
int32_t probeShapeId(uint32_t h, const Shape &s);
/** Traps (WASM conformance only: it would kill a native process). */
void probeTrap();
bool probeFailed();
util::string probeLastError();
void probeRelease(uint32_t h);

int32_t addInts(int32_t a, int32_t b);
double scaleD(double v, double s);
bool isEven(int32_t v);
int64_t bigProduct(int64_t a, int64_t b);
Color nextColor(Color c);
float vecLength(const Vec2 &v);
Vec2 midpoint(const Vec2 &a, const Vec2 &b);
void moveBy(Vec2 *v, float dx, float dy);
Shape *makeShape(int32_t id);
Shape *sharedShape();
Shape *maybeShape(bool give);
int32_t liveShapes();
const char *colorName(Color c);
int32_t textLength(const char *s);
util::string greet(const util::string &name);
float sumFloats(std::span<const float> v);
void fillSquares(std::span<int32_t> out);
util::Vector<int32_t> range(int32_t n);
util::Vector<uint8_t> bytes(int32_t n);
float sumVector(const util::Vector<float> &v);
int32_t overloaded(int32_t v);
float overloaded(float v);

template <typename T> Box<T> *makeBox(T lo, T hi)
{
  Box<T> *b = alloc::New<Box<T>>("fixture::Box");
  b->lo = lo;
  b->hi = hi;
  return b;
}
template <typename T> T boxExtent(const Box<T> &b)
{
  return b.extent();
}
/** Instantiations differ only in a span, which JS passes as an array, so the
 * call needs a selector key. */
template <typename T> T sumSpan(std::span<const T> v)
{
  T sum = 0;
  for (T x : v) {
    sum += x;
  }
  return sum;
}

/** Registers everything above with `mgr`. */
void registerFixture(binding::BindingManager *mgr);

/** The process-wide manager the WASM module and N-API addon expose. */
binding::BindingManager *fixtureManager();

} // namespace fixture
