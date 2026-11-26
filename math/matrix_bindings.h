#pragma once

/* Bindings for Matrix, Quat and ClipDepth. Not in math_bindings.h, which binding.h
 * includes everywhere, because matrix.h brings in Eigen. */

#include "../binding/binding.h"
#include "../util/alloc.h"
#include "./matrix.h"
#include "./quat.h"
#include "./transform.h"

namespace litestl::binding {

/** ClipDepth, bound as an enum so a host can bind members and arguments of that type. */
template <> struct Binder<math::ClipDepth> {
  static const BindingBase *bind()
  {
    static types::Enum *e = nullptr;
    if (!e) {
      alloc::PermanentGuard guard;
      e = new types::Enum("litestl::math::ClipDepth", sizeof(math::ClipDepth));
      e->addItem("ZeroToOne", math::ClipDepth::ZeroToOne);
      e->addItem("MinusOneToOne", math::ClipDepth::MinusOneToOne);
    }
    return e;
  }
};

namespace detail {
template <typename T> struct MathElements;
template <typename F, int N> struct MathElements<math::Matrix<F, N>> {
  using Float = F;
  static constexpr int count = N * N;
};
template <typename F> struct MathElements<math::Quat<F>> {
  using Float = F;
  static constexpr int count = 4;
};

/* Scripts read and write elements by storage index. Out of range reads 0 and writes
 * nothing. */
template <typename T> typename MathElements<T>::Float mathElementAt(const T &v, int32_t i)
{
  using Float = typename MathElements<T>::Float;
  if (i < 0 || i >= MathElements<T>::count) {
    return Float(0);
  }
  return reinterpret_cast<const Float *>(&v)[i];
}
template <typename T>
void mathElementPut(T &v, int32_t i, typename MathElements<T>::Float value)
{
  if (i >= 0 && i < MathElements<T>::count) {
    reinterpret_cast<typename MathElements<T>::Float *>(&v)[i] = value;
  }
}

template <typename T> types::Struct<T> *bindMathElements(const char *name)
{
  using Float = typename MathElements<T>::Float;
  static_assert(sizeof(T) == sizeof(Float) * MathElements<T>::count);
  types::Struct<T> *st = new types::Struct<T>(name, sizeof(T));
  st->add("vec", 0, new types::Array(Bind<Float>(), MathElements<T>::count));
  BIND_STRUCT_DEFAULT_CONSTRUCTOR(st);
  BIND_STRUCT_COPY_CONSTRUCTOR(st);
  BIND_ADAPTOR_METHOD(st, at, (mathElementAt<T>), MARGS("i"));
  BIND_ADAPTOR_METHOD(st, put, (mathElementPut<T>), MARGS("i", "value"));
  return st;
}
} // namespace detail

#define LITESTL_BIND_MATH_TYPE(type)                                                     \
  template <> struct Binder<math::type> {                                                \
    static types::Struct<math::type> *bind()                                             \
    {                                                                                    \
      return detail::bindMathElements<math::type>("litestl::math::" #type);              \
    }                                                                                    \
  };

LITESTL_BIND_MATH_TYPE(mat3f)
LITESTL_BIND_MATH_TYPE(mat4f)
LITESTL_BIND_MATH_TYPE(mat3d)
LITESTL_BIND_MATH_TYPE(mat4d)
LITESTL_BIND_MATH_TYPE(quatf)
LITESTL_BIND_MATH_TYPE(quatd)

#undef LITESTL_BIND_MATH_TYPE

} // namespace litestl::binding
