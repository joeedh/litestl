#pragma once

#include "math/matrix.h"
#include "math/vector.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace litestl::math {

/**
 * A quaternion, stored x, y, z, w. Rotations are unit quaternions acting on column
 * vectors: `(a * b).rotate(v) == a.rotate(b.rotate(v))`, and toMatrix3() agrees with the
 * matrix product and with transformVector() in math/transform.h.
 */
template <typename Float> class Quat {
public:
  using value_type = Float;
  using Vec3 = Vec<Float, 3>;
  static constexpr int size = 4;

  /** The identity rotation. */
  constexpr Quat() : vec_{Float(0), Float(0), Float(0), Float(1)}
  {
  }
  constexpr Quat(Float x, Float y, Float z, Float w) : vec_{x, y, z, w}
  {
  }

  /** Rotation by `angle` radians around `axis`, which need not be unit length. A zero
   * or non-finite axis gives the identity. */
  static Quat fromAxisAngle(const Vec3 &axis, Float angle)
  {
    const Float s = std::sin(angle * Float(0.5)), c = std::cos(angle * Float(0.5));
    Quat unit(axis[0], axis[1], axis[2], Float(0));
    if (!unit.tryNormalize()) {
      return Quat();
    }
    return Quat(unit[0] * s, unit[1] * s, unit[2] * s, c);
  }

  /** The rotation of the upper-left 3x3 of `m`, whose columns must be orthonormal and
   * right-handed. */
  template <int N> static Quat fromMatrix(const Matrix<Float, N> &m)
  {
    static_assert(N == 3 || N == 4);
    const Float *d = m;
    /* r(i, j) is row i, column j. */
    const auto r = [&](int i, int j) { return d[j * N + i]; };
    const Float trace = r(0, 0) + r(1, 1) + r(2, 2);
    Quat q;
    if (trace > Float(0)) {
      q = Quat(r(2, 1) - r(1, 2), r(0, 2) - r(2, 0), r(1, 0) - r(0, 1), Float(1) + trace);
    } else {
      int i = r(1, 1) > r(0, 0) ? 1 : 0;
      i = r(2, 2) > r(i, i) ? 2 : i;
      const int j = (i + 1) % 3, k = (i + 2) % 3;
      q[i] = Float(1) + r(i, i) - r(j, j) - r(k, k);
      q[j] = r(j, i) + r(i, j);
      q[k] = r(k, i) + r(i, k);
      q[3] = r(k, j) - r(j, k);
    }
    q.normalize();
    return q;
  }

  constexpr Float &operator[](int i)
  {
    return vec_[i];
  }
  constexpr Float operator[](int i) const
  {
    return vec_[i];
  }

  constexpr Quat operator*(const Quat &b) const
  {
    const Quat &a = *this;
    return Quat(a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
                a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
                a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
                a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]);
  }
  constexpr Quat &operator*=(const Quat &b)
  {
    *this = *this * b;
    return *this;
  }
  constexpr Quat operator-() const
  {
    return Quat(-vec_[0], -vec_[1], -vec_[2], -vec_[3]);
  }

  /** The inverse of a unit quaternion. */
  constexpr Quat conjugated() const
  {
    return Quat(-vec_[0], -vec_[1], -vec_[2], vec_[3]);
  }

  constexpr Float dot(const Quat &b) const
  {
    return vec_[0] * b[0] + vec_[1] * b[1] + vec_[2] * b[2] + vec_[3] * b[3];
  }

  /** Scales to unit length; a zero or non-finite quaternion becomes the identity. */
  Quat &normalize()
  {
    if (!tryNormalize()) {
      *this = Quat();
    }
    return *this;
  }
  Quat normalized() const
  {
    Quat q = *this;
    q.normalize();
    return q;
  }

  /** Rotates `v`. The quaternion must be unit length. */
  constexpr Vec3 rotate(const Vec3 &v) const
  {
    const Vec3 u(vec_[0], vec_[1], vec_[2]);
    const Vec3 t = u.cross(v) * Float(2);
    return v + t * vec_[3] + u.cross(t);
  }

  Matrix<Float, 3> toMatrix3() const
  {
    Matrix<Float, 3> m;
    fill<3>(m);
    return m;
  }
  Matrix<Float, 4> toMatrix4() const
  {
    Matrix<Float, 4> m;
    m.identity();
    fill<4>(m);
    return m;
  }

  /** The rotation's unit axis and its angle in [0, pi]. The identity's axis is x. */
  void toAxisAngle(Vec3 &axis, Float &angle) const
  {
    const Float sign = vec_[3] < Float(0) ? Float(-1) : Float(1);
    Quat unit(vec_[0] * sign, vec_[1] * sign, vec_[2] * sign, Float(0));
    const Float s = std::hypot(vec_[0], vec_[1], vec_[2]);
    angle = Float(2) * std::atan2(s, vec_[3] * sign);
    axis = unit.tryNormalize() ? Vec3(unit[0], unit[1], unit[2])
                               : Vec3(Float(1), Float(0), Float(0));
  }

private:
  /** False, and unchanged, for zero and non-finite values. */
  bool tryNormalize()
  {
    Float big = Float(0);
    for (int i = 0; i < 4; i++) {
      if (!std::isfinite(vec_[i])) {
        return false;
      }
      big = (std::max)(big, std::fabs(vec_[i]));
    }
    if (!(big > Float(0))) {
      return false;
    }
    /* Scaled first: the squares of very large or small values leave the range. */
    Quat q(vec_[0] / big, vec_[1] / big, vec_[2] / big, vec_[3] / big);
    const Float len = std::sqrt(q.dot(q));
    for (int i = 0; i < 4; i++) {
      vec_[i] = q[i] / len;
    }
    return true;
  }

  template <int N> void fill(Matrix<Float, N> &m) const
  {
    const Float x = vec_[0], y = vec_[1], z = vec_[2], w = vec_[3];
    const Float two = Float(2), one = Float(1);
    m[0][0] = one - two * (y * y + z * z);
    m[0][1] = two * (x * y + w * z);
    m[0][2] = two * (x * z - w * y);
    m[1][0] = two * (x * y - w * z);
    m[1][1] = one - two * (x * x + z * z);
    m[1][2] = two * (y * z + w * x);
    m[2][0] = two * (x * z + w * y);
    m[2][1] = two * (y * z - w * x);
    m[2][2] = one - two * (x * x + y * y);
  }

  Float vec_[4];
};

/** Spherical interpolation between unit quaternions, along the shorter arc. */
template <typename Float> Quat<Float> slerp(const Quat<Float> &a, Quat<Float> b, Float t)
{
  Float c = a.dot(b);
  if (c < Float(0)) {
    b = -b;
    c = -c;
  }
  Float wa = Float(1) - t, wb = t;
  /* Below this angle the linear weights are exact to rounding. */
  if (c < Float(1) - std::numeric_limits<Float>::epsilon() * Float(8)) {
    /* From the half chord: acos loses half the digits near 1. */
    const Quat<Float> d(b[0] - a[0], b[1] - a[1], b[2] - a[2], b[3] - a[3]);
    const Float half = std::asin((std::min)(Float(1), std::sqrt(d.dot(d)) / Float(2)));
    const Float angle = Float(2) * half, s = std::sin(angle);
    wa = std::sin((Float(1) - t) * angle) / s;
    wb = std::sin(t * angle) / s;
  }
  Quat<Float> r;
  for (int i = 0; i < 4; i++) {
    r[i] = a[i] * wa + b[i] * wb;
  }
  r.normalize();
  return r;
}

using quatf = Quat<float>;
using quatd = Quat<double>;

} // namespace litestl::math
