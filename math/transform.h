#pragma once

/*
 * Affine and projective helpers over column-major Matrix<Float, 4>: `m[c][r]` is column
 * c, row r, and the translation is m[3]. The convention is that of Matrix's matrix
 * product: `(a * b)` applies b first. (Matrix's own `matrix * vector` operator multiplies
 * by the transpose; these functions don't use it.)
 */

#include "math/matrix.h"
#include "math/quat.h"
#include "math/vector.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace litestl::math {

/** `m * v`. */
template <typename Float>
Vec<Float, 4> transformHomogeneous(const Matrix<Float, 4> &m, const Vec<Float, 4> &v)
{
  const Float *d = m;
  Vec<Float, 4> r;
  for (int row = 0; row < 4; row++) {
    r[row] = d[row] * v[0] + d[4 + row] * v[1] + d[8 + row] * v[2] + d[12 + row] * v[3];
  }
  return r;
}

/** A point through an affine matrix; its last row is ignored. */
template <typename Float>
Vec<Float, 3> transformPoint(const Matrix<Float, 4> &m, const Vec<Float, 3> &p)
{
  const Float *d = m;
  Vec<Float, 3> r;
  for (int row = 0; row < 3; row++) {
    r[row] = d[row] * p[0] + d[4 + row] * p[1] + d[8 + row] * p[2] + d[12 + row];
  }
  return r;
}

/** A direction: the translation doesn't apply. */
template <typename Float>
Vec<Float, 3> transformVector(const Matrix<Float, 4> &m, const Vec<Float, 3> &v)
{
  const Float *d = m;
  Vec<Float, 3> r;
  for (int row = 0; row < 3; row++) {
    r[row] = d[row] * v[0] + d[4 + row] * v[1] + d[8 + row] * v[2];
  }
  return r;
}

/**
 * The plane `n.xyz . p + n.w = 0` of m's target space, in m's source space: a source
 * point is on the same side of the result as its image is of `plane`. It is a product
 * with the transpose, so it holds for a singular `m` too.
 */
template <typename Float>
Vec<Float, 4> planeToLocal(const Matrix<Float, 4> &m, const Vec<Float, 4> &plane)
{
  const Float *d = m;
  Vec<Float, 4> r;
  for (int c = 0; c < 4; c++) {
    const Float *col = d + c * 4;
    r[c] = col[0] * plane[0] + col[1] * plane[1] + col[2] * plane[2] + col[3] * plane[3];
  }
  return r;
}

/** `translate(t) * rotate(q) * scale(s)`, with `q` a unit quaternion. */
template <typename Float>
Matrix<Float, 4>
composeTrs(const Vec<Float, 3> &t, const Quat<Float> &q, const Vec<Float, 3> &s)
{
  Matrix<Float, 4> m = q.toMatrix4();
  for (int c = 0; c < 3; c++) {
    for (int r = 0; r < 3; r++) {
      m[c][r] *= s[c];
    }
    m[3][c] = t[c];
  }
  return m;
}

/**
 * Splits an affine matrix into translation, rotation and scale.
 * - A reflection becomes a negative x scale.
 * - Zero columns have scale 0, and the rotation is completed from the others.
 * - Returns the shear: the largest |cosine| between two columns, 0 for an exact TRS
 *   matrix. With shear the rotation follows the x column, then y, and the parts don't
 *   compose back to `m`.
 * - Non-finite input gives the identity rotation.
 */
template <typename Float>
Float decomposeTrs(const Matrix<Float, 4> &m,
                   Vec<Float, 3> &translation,
                   Quat<Float> &rotation,
                   Vec<Float, 3> &scale)
{
  using Vec3 = Vec<Float, 3>;
  const Float *d = m;
  translation = Vec3(d[12], d[13], d[14]);

  Vec3 col[3];
  bool has[3];
  bool finite = true;
  for (int c = 0; c < 3; c++) {
    const Float *p = d + c * 4;
    const Float big =
        (std::max)((std::max)(std::fabs(p[0]), std::fabs(p[1])), std::fabs(p[2]));
    scale[c] = std::hypot(p[0], p[1], p[2]);
    finite &= std::isfinite(scale[c]);
    has[c] = big > Float(0);
    if (has[c]) {
      /* Scaled first: a denormal length has lost digits. */
      col[c] = Vec3(p[0] / big, p[1] / big, p[2] / big);
      col[c] = col[c] / std::hypot(col[c][0], col[c][1], col[c][2]);
    }
  }
  rotation = Quat<Float>();
  if (!finite) {
    return Float(0);
  }

  Float shear = Float(0);
  for (int a = 0; a < 3; a++) {
    for (int b = a + 1; b < 3; b++) {
      if (has[a] && has[b]) {
        shear = (std::max)(shear, std::fabs(col[a].dot(col[b])));
      }
    }
  }
  if (has[0] && has[1] && has[2] && col[0].cross(col[1]).dot(col[2]) < Float(0)) {
    scale[0] = -scale[0];
    col[0] = col[0] * Float(-1);
  }

  /* Orthonormal axes in the order x, y, z. A column that is zero or depends on the ones
   * before it doesn't define its axis. */
  Vec3 axis[3];
  bool done[3] = {false, false, false};
  const auto accept = [&](int c, Vec3 v) {
    for (int pass = 0; pass < 2; pass++) { /* twice: one pass leaves an angle behind */
      for (int o = 0; o < 3; o++) {
        if (done[o]) {
          v = v - axis[o] * v.dot(axis[o]);
        }
      }
      const Float len = v.length();
      if (!(len > std::numeric_limits<Float>::epsilon() * Float(64))) {
        return;
      }
      v = v / len;
    }
    axis[c] = v;
    done[c] = true;
  };
  for (int c = 0; c < 3; c++) {
    if (has[c]) {
      accept(c, col[c]);
    }
  }
  const bool fromColumn[3] = {done[0], done[1], done[2]};
  for (int c = 0; c < 3; c++) {
    for (int k = 0; k < 3 && !done[c]; k++) {
      Vec3 unit;
      unit[(c + k) % 3] = Float(1);
      accept(c, unit);
    }
  }
  if (axis[0].cross(axis[1]).dot(axis[2]) < Float(0)) {
    /* With three independent columns this was settled above, so some axis was completed
     * and its sign is free. */
    const int c = !fromColumn[0] ? 0 : !fromColumn[1] ? 1 : 2;
    axis[c] = axis[c] * Float(-1);
  }
  Matrix<Float, 3> basis;
  for (int c = 0; c < 3; c++) {
    for (int r = 0; r < 3; r++) {
      basis[c][r] = axis[c][r];
    }
  }
  rotation = Quat<Float>::fromMatrix(basis);
  return shear;
}

/**
 * The inverse of `m`, or false with `out` unchanged when `m` is singular or not finite,
 * or its inverse overflows. Rows and columns are scaled by powers of two first, so a
 * matrix isn't singular just because its entries are tiny or unlike in size. `out` may be
 * `m`.
 */
template <typename Float, int N>
bool tryInverse(const Matrix<Float, N> &m, Matrix<Float, N> &out)
{
  const Float *d = m;
  /* a[row][column], with the identity appended. */
  Float a[N][2 * N];
  int rowExp[N], colExp[N];
  for (int r = 0; r < N; r++) {
    for (int c = 0; c < N; c++) {
      a[r][c] = d[c * N + r];
      a[r][N + c] = r == c ? Float(1) : Float(0);
      if (!std::isfinite(a[r][c])) {
        return false;
      }
    }
  }
  for (int r = 0; r < N; r++) {
    Float big = Float(0);
    for (int c = 0; c < N; c++) {
      big = (std::max)(big, std::fabs(a[r][c]));
    }
    if (big == Float(0)) {
      return false;
    }
    int e = 0;
    std::frexp(big, &e);
    rowExp[r] = -e;
    for (int c = 0; c < N; c++) {
      a[r][c] = std::ldexp(a[r][c], rowExp[r]);
    }
  }
  for (int c = 0; c < N; c++) {
    Float big = Float(0);
    for (int r = 0; r < N; r++) {
      big = (std::max)(big, std::fabs(a[r][c]));
    }
    if (big == Float(0)) {
      return false;
    }
    int e = 0;
    std::frexp(big, &e);
    colExp[c] = -e;
    for (int r = 0; r < N; r++) {
      a[r][c] = std::ldexp(a[r][c], colExp[c]);
    }
  }

  const Float tiny = std::numeric_limits<Float>::epsilon() * Float(N);
  for (int c = 0; c < N; c++) {
    int pivot = c;
    for (int r = c + 1; r < N; r++) {
      if (std::fabs(a[r][c]) > std::fabs(a[pivot][c])) {
        pivot = r;
      }
    }
    if (!(std::fabs(a[pivot][c]) > tiny)) {
      return false;
    }
    if (pivot != c) {
      for (int k = 0; k < 2 * N; k++) {
        std::swap(a[c][k], a[pivot][k]);
      }
    }
    const Float inv = Float(1) / a[c][c];
    for (int k = 0; k < 2 * N; k++) {
      a[c][k] *= inv;
    }
    for (int r = 0; r < N; r++) {
      const Float f = a[r][c];
      if (r == c || f == Float(0)) {
        continue;
      }
      for (int k = 0; k < 2 * N; k++) {
        a[r][k] -= f * a[c][k];
      }
    }
  }

  Matrix<Float, N> result;
  for (int r = 0; r < N; r++) {
    for (int c = 0; c < N; c++) {
      const Float v = std::ldexp(a[r][N + c], colExp[r] + rowExp[c]);
      if (!std::isfinite(v)) {
        return false;
      }
      result[c][r] = v;
    }
  }
  out = result;
  return true;
}

/** The depth range of normalized device coordinates. */
enum class ClipDepth : int32_t { ZeroToOne = 0, MinusOneToOne = 1 };

namespace detail {
/** The clip depths of the near and far ends. */
template <typename Float> struct DepthEnds {
  Float atNear, atFar;
  DepthEnds(ClipDepth depth, bool reversed)
  {
    atNear = depth == ClipDepth::ZeroToOne ? Float(0) : Float(-1);
    atFar = Float(1);
    if (reversed) {
      std::swap(atNear, atFar);
    }
  }
};
} // namespace detail

/**
 * A right-handed perspective projection: the camera looks down -z, and +y is up in clip
 * space. `fovY` is in radians, `aspect` is width over height. `zFar` may be infinity.
 */
template <typename Float>
Matrix<Float, 4> perspective(Float fovY,
                             Float aspect,
                             Float zNear,
                             Float zFar,
                             ClipDepth depth = ClipDepth::ZeroToOne,
                             bool reversed = false)
{
  const detail::DepthEnds<Float> ends(depth, reversed);
  const Float f = Float(1) / std::tan(fovY * Float(0.5));
  Matrix<Float, 4> m;
  m[0][0] = f / aspect;
  m[1][1] = f;
  m[2][3] = Float(-1);
  /* clip z = A z + B, with -A n + B = atNear n and -A f + B = atFar f. */
  if (std::isinf(zFar)) {
    m[2][2] = -ends.atFar;
    m[3][2] = (ends.atNear - ends.atFar) * zNear;
  } else {
    const Float inv = Float(1) / (zNear - zFar);
    m[2][2] = (ends.atFar * zFar - ends.atNear * zNear) * inv;
    m[3][2] = (ends.atFar - ends.atNear) * zNear * zFar * inv;
  }
  return m;
}

/** A right-handed orthographic projection of the box: the camera looks down -z. */
template <typename Float>
Matrix<Float, 4> orthographic(Float left,
                              Float right,
                              Float bottom,
                              Float top,
                              Float zNear,
                              Float zFar,
                              ClipDepth depth = ClipDepth::ZeroToOne,
                              bool reversed = false)
{
  const detail::DepthEnds<Float> ends(depth, reversed);
  Matrix<Float, 4> m;
  m[0][0] = Float(2) / (right - left);
  m[1][1] = Float(2) / (top - bottom);
  m[2][2] = (ends.atFar - ends.atNear) / (zNear - zFar);
  m[3][0] = (right + left) / (left - right);
  m[3][1] = (top + bottom) / (bottom - top);
  m[3][2] = (ends.atNear * zFar - ends.atFar * zNear) / (zFar - zNear);
  m[3][3] = Float(1);
  return m;
}

/**
 * The right-handed world-to-view matrix of a camera at `eye` that looks at `target` down
 * its -z axis. `up` need not be unit length; when it is zero or parallel to the view
 * direction another one is chosen.
 */
template <typename Float>
Matrix<Float, 4>
lookAt(const Vec<Float, 3> &eye, const Vec<Float, 3> &target, const Vec<Float, 3> &up)
{
  using Vec3 = Vec<Float, 3>;
  /* Not Vec::normalize(), which zeroes lengths under 1e-8. */
  const auto unit = [](Vec3 v, const Vec3 &fallback) {
    Float big = Float(0);
    for (int i = 0; i < 3; i++) {
      big = (std::max)(big, std::fabs(v[i]));
    }
    if (!(big > Float(0)) || !std::isfinite(big)) {
      return fallback;
    }
    v = v / big;
    return v / v.length();
  };
  const Vec3 z = unit(eye - target, Vec3(Float(0), Float(0), Float(1)));
  Vec3 x = unit(up, Vec3()).cross(z);
  if (x.length() < std::numeric_limits<Float>::epsilon() * Float(16)) {
    int least = 0;
    for (int i = 1; i < 3; i++) {
      least = std::fabs(z[i]) < std::fabs(z[least]) ? i : least;
    }
    Vec3 other;
    other[least] = Float(1);
    x = other.cross(z);
  }
  x = unit(x, Vec3(Float(1), Float(0), Float(0)));
  const Vec3 rows[3] = {x, z.cross(x), z};
  Matrix<Float, 4> m;
  for (int r = 0; r < 3; r++) {
    for (int c = 0; c < 3; c++) {
      m[c][r] = rows[r][c];
    }
    m[3][r] = -rows[r].dot(eye);
  }
  m[3][3] = Float(1);
  return m;
}

} // namespace litestl::math
