#pragma once

/**
 * Small fixed-size linear algebra vector class.
 *
 * @tparam T The scalar element type (e.g. float, double, int32_t).
 * @tparam vec_size The number of components in the vector.
 *
 * Provides component-wise arithmetic, dot product, normalization,
 * distance, interpolation, and 2D rotation. Common type aliases
 * (e.g. float2, double3, int4) are generated via the DEF_VECS macro.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <utility>

#include "util/compiler_util.h"
#include "util/type_tags.h"

namespace litestl::math {
template <typename T, int vec_size> class Vec {
public:
  using value_type = T;
  using is_math_vector = std::true_type;

  static const int size = vec_size;

  /** Default constructor. Initializes all components to zero. */
  constexpr Vec()
  {
    vec_[0] = vec_[1] = T(0);
  }

  /** Constructs a vector from two components. Remaining components are zeroed. */
  constexpr Vec(T a, T b)
  {
    vec_[0] = a;
    vec_[1] = b;

    for (int i = 2; i < vec_size; i++) {
      vec_[i] = 0.0f;
    }
  }

  /** Constructs a vector from three components. Remaining components are zeroed. */
  constexpr Vec(T a, T b, T c)
  {
    vec_[0] = a;
    vec_[1] = b;
    vec_[2] = c;

    for (int i = 3; i < vec_size; i++) {
      vec_[i] = 0.0f;
    }
  }

  /** Constructs a vector from four components. Remaining components are zeroed. */
  constexpr Vec(T a, T b, T c, T d)
  {
    vec_[0] = a;
    vec_[1] = b;
    vec_[2] = c;
    vec_[3] = d;

    for (int i = 4; i < vec_size; i++) {
      vec_[i] = 0.0f;
    }
  }

  /** Broadcasts a single scalar value to all components. */
  constexpr Vec(T single)
  {
    for (int i = 0; i < vec_size; i++) {
      vec_[i] = single;
    }
  }

  /** Constructs a vector by copying vec_size elements from a raw pointer. */
  constexpr Vec(const T *value)
  {
    for (int i = 0; i < vec_size; i++) {
      vec_[i] = value[i];
    }
  }

  /** Copy constructor. */
  constexpr Vec(const Vec &b)
  {
    for (int i = 0; i < vec_size; i++) {
      vec_[i] = b.vec_[i];
    }
  }

  /** Copy constructor (non-const overload). */
  constexpr Vec(Vec &b)
  {
    for (int i = 0; i < vec_size; i++) {
      vec_[i] = b.vec_[i];
    }
  }

  /** Sets all components to zero. Returns *this for chaining. */
  inline constexpr Vec &zero()
  {
    for (int i = 0; i < vec_size; i++) {
      vec_[i] = T(0);
    }

    return *this;
  }

  /** Returns a mutable reference to the component at @p idx. */
  inline constexpr T &operator[](int idx)
  {
    return vec_[idx];
  }

  /** Returns the component value at @p idx. */
  inline constexpr T operator[](int idx) const
  {
    return vec_[idx];
  }

/**
 * Defines component-wise arithmetic operators for a given operator symbol.
 *
 * For each op, generates: Vec op Vec, Vec op scalar, Vec op= scalar, Vec op= Vec.
 */
#ifdef VEC_OP_DEF
#undef VEC_OP_DEF
#endif

#define VEC_OP_DEF(op)                                                                   \
  inline constexpr Vec operator op(const Vec &b) const                                   \
  {                                                                                      \
    Vec r;                                                                               \
    for (int i = 0; i < vec_size; i++) {                                                 \
      r[i] = vec_[i] op b.vec_[i];                                                       \
    }                                                                                    \
    return r;                                                                            \
  }                                                                                      \
  inline constexpr Vec operator op(T b) const                                            \
  {                                                                                      \
    Vec r;                                                                               \
    for (int i = 0; i < vec_size; i++) {                                                 \
      r[i] = vec_[i] op b;                                                               \
    }                                                                                    \
    return r;                                                                            \
  }                                                                                      \
  inline constexpr Vec &operator op## = (T b)                                            \
  {                                                                                      \
    for (int i = 0; i < vec_size; i++) {                                                 \
      vec_[i] op## = b;                                                                  \
    }                                                                                    \
    return *this;                                                                        \
  }                                                                                      \
  inline constexpr Vec &operator op## = (const Vec b)                                    \
  {                                                                                      \
    for (int i = 0; i < vec_size; i++) {                                                 \
      vec_[i] op## = b.vec_[i];                                                          \
    }                                                                                    \
    return *this;                                                                        \
  }

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-token-paste"
#endif

  VEC_OP_DEF(*)
  VEC_OP_DEF(/)
  VEC_OP_DEF(+)
  VEC_OP_DEF(-)

#ifdef __clang__
#pragma clang diagnostic pop
#endif

  /** Component-wise minimum with @p b. Modifies in-place. */
  constexpr Vec &min(const Vec &b)
  {
    for (int i = 0; i < vec_size; i++) {
      vec_[i] = std::min(vec_[i], b.vec_[i]);
    }

    return *this;
  }

  /** Component-wise maximum with @p b. Modifies in-place. */
  constexpr Vec &max(const Vec &b)
  {
    for (int i = 0; i < vec_size; i++) {
      vec_[i] = std::max(vec_[i], b.vec_[i]);
    }

    return *this;
  }

  /** Applies std::floor to each component. Modifies in-place. */
  constexpr Vec &floor()
  {
    for (int i = 0; i < vec_size; i++) {
      vec_[i] = std::floor(vec_[i]);
    }

    return *this;
  }

  /** Applies std::ceil to each component. Modifies in-place. */
  constexpr Vec &ceil()
  {
    for (int i = 0; i < vec_size; i++) {
      vec_[i] = std::ceil(vec_[i]);
    }

    return *this;
  }

  /** Applies std::abs to each component. Modifies in-place. */
  constexpr Vec &abs()
  {
    for (int i = 0; i < vec_size; i++) {
      vec_[i] = std::abs(vec_[i]);
    }

    return *this;
  }

  /** Computes the fractional part of each component. Modifies in-place. */
  constexpr Vec &fract()
  {
    for (int i = 0; i < vec_size; i++) {
      vec_[i] -= std::floor(vec_[i]);
    }

    return *this;
  }

  /** Returns the dot product of this vector with @p b. */
  T dot(const Vec &b) const
  {
    T sum = T(0);
    for (int i = 0; i < vec_size; i++) {
      sum += vec_[i] * b.vec_[i];
    }

    return sum;
  }

  /**
   * Normalizes the vector to unit length in-place.
   *
   * @return The original length before normalization. If the length is
   *         near zero (< 1e-8), the vector is zeroed and 0 is returned.
   */
  constexpr T normalize()
  {
    T len = length();
    if (len > 0.00000001) {
      double mul = 1.0 / double(len);

      for (int i = 0; i < vec_size; i++) {
        vec_[i] = T(double(vec_[i]) * mul);
      }
    } else {
      zero();
      len = 0.0;
    }

    return len;
  }

  /** Returns the Euclidean length (L2 norm) of the vector. */
  constexpr T length() const
  {
    return std::sqrt(dot(*this));
  }

  /** Returns the squared length of the vector (avoids the sqrt). */
  constexpr T lengthSqr() const
  {
    return dot(*this);
  }

  /** Returns the Euclidean distance to @p b. */
  constexpr T distance(const Vec &b) const
  {
    return std::sqrt(distanceSqr(b));
  }

  /** Returns the squared Euclidean distance to @p b (avoids the sqrt). */
  constexpr T distanceSqr(const Vec &b) const
  {
    return (b - *this).lengthSqr();
  }

  /**
   * Rotates the first two components around @p center by angle @p th (radians).
   * Only meaningful for vec_size >= 2.
   */
  constexpr Vec &rotate2d(Vec center, double th)
  {
    if constexpr (vec_size > 1) {
      double cosTh = std::cos(th);
      double sinTh = std::sin(th);
      double x = vec_[0] - center[0];
      double y = vec_[1] - center[1];

      vec_[0] = cosTh * x - sinTh * y;
      vec_[1] = cosTh * y + sinTh * x;
    }

    return *this;
  }

  /**
   * Linearly interpolates this vector toward @p b by @p factor.
   * A factor of 0 leaves this unchanged; a factor of 1 makes it equal to @p b.
   * Modifies in-place.
   */
  constexpr Vec &interp(const Vec &b, double factor)
  {
    for (int i = 0; i < vec_size; i++) {
      vec_[i] += T(double(b.vec_[i] - vec_[i]) * factor);
    }

    return *this;
  }

  /** Implicit conversion to a raw pointer to the underlying array. */
  operator T *()
  {
    return vec_;
  }

  /** Prints the vector components to stdout in the format "(x y z ...)". */
  void print()
  {
    printf("(");
    for (int i = 0; i < vec_size; i++) {
      if (i > 0) {
        printf(" ");
      }
      printf("%.4f", vec_[i]);
    }
    printf(")");
  }

  /** Negates all components in-place. */
  constexpr Vec &negate()
  {
    for (int i = 0; i < vec_size; i++) {
      vec_[i] = -vec_[i];
    }
    return *this;
  }

private:
  T vec_[vec_size];
};

#ifdef DEF_VECS
#undef DEF_VECS
#endif
} // namespace litestl::math

/**
 * Generates 1D through 4D Vec type aliases and marks them as simple types.
 *
 * For example, DEF_VECS(float, float) creates float1, float2, float3, float4.
 */
#define DEF_VECS(type, name)                                                             \
  namespace litestl::math {                                                              \
  using name##1 = Vec<type, 1>;                                                          \
  using name##2 = Vec<type, 2>;                                                          \
  using name##3 = Vec<type, 3>;                                                          \
  using name##4 = Vec<type, 4>;                                                          \
  }                                                                                      \
  force_type_is_simple(litestl::math::name##1);                                          \
  force_type_is_simple(litestl::math::name##2);                                          \
  force_type_is_simple(litestl::math::name##3);                                          \
  force_type_is_simple(litestl::math::name##4);

DEF_VECS(float, float);
DEF_VECS(double, double);
DEF_VECS(int32_t, int);
DEF_VECS(uint32_t, uint);
DEF_VECS(int16_t, short);
DEF_VECS(uint16_t, ushort);
DEF_VECS(int8_t, char);
DEF_VECS(uint8_t, uchar);

#undef DEF_VECS
