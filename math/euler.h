#pragma once

#include "quat.h"
#include "vector.h"

#include <cmath>
#include <cstdint>
#include <limits>

namespace litestl::math {

/** The order the axes are applied in: XYZ turns around x first, so its matrix is
 * `Rz * Ry * Rx`. */
enum class EulerOrder : int32_t { XYZ = 0, XZY = 1, YXZ = 2, YZX = 3, ZXY = 4, ZYX = 5 };

namespace detail {
struct EulerAxes {
  int i, j, k;
  /** The permutation (i, j, k) is even. */
  bool even;
};
constexpr EulerAxes eulerAxes(EulerOrder order)
{
  switch (order) {
  case EulerOrder::XZY:
    return {0, 2, 1, false};
  case EulerOrder::YXZ:
    return {1, 0, 2, false};
  case EulerOrder::YZX:
    return {1, 2, 0, true};
  case EulerOrder::ZXY:
    return {2, 0, 1, true};
  case EulerOrder::ZYX:
    return {2, 1, 0, false};
  default:
    return {0, 1, 2, true};
  }
}
} // namespace detail

/** `angles[a]` is the rotation around axis a, in radians, whatever the order. */
template <typename Float>
Quat<Float> eulerToQuat(const Vec<Float, 3> &angles, EulerOrder order)
{
  const auto axis = [&](int a) {
    Quat<Float> q;
    q[a] = std::sin(angles[a] * Float(0.5));
    q[3] = std::cos(angles[a] * Float(0.5));
    return q;
  };
  const detail::EulerAxes o = detail::eulerAxes(order);
  return axis(o.k) * axis(o.j) * axis(o.i);
}

/**
 * Euler angles of a unit quaternion. There are infinitely many (whole turns, and two
 * families): the result is the one nearest to `hint`, so angles stay continuous while
 * they change. At gimbal lock the last axis's angle is taken from `hint`.
 */
template <typename Float>
Vec<Float, 3> quatToEuler(const Quat<Float> &q,
                          EulerOrder order,
                          const Vec<Float, 3> &hint = Vec<Float, 3>())
{
  const Float turn = Float(6.28318530717958647692528676655900577L);
  const detail::EulerAxes o = detail::eulerAxes(order);
  const Float s = o.even ? Float(1) : Float(-1);
  const Matrix<Float, 3> m = q.toMatrix3();
  const Float *d = m;
  const auto r = [&](int row, int col) { return d[col * 3 + row]; };

  const Float cosMid = std::hypot(r(o.i, o.i), r(o.j, o.i));
  const bool locked = cosMid < std::numeric_limits<Float>::epsilon() * Float(16);

  Vec<Float, 3> best;
  Float bestDist = Float(0);
  for (int family = 0; family < 2; family++) {
    const Float flip = family ? Float(-1) : Float(1);
    const Float last =
        locked ? hint[o.k] : std::atan2(flip * s * r(o.j, o.i), flip * r(o.i, o.i));
    const Float mid = std::atan2(-s * r(o.k, o.i), flip * cosMid);
    /* The first angle from Rlast^-1 * R, which stays well conditioned near the lock. */
    const Float sn = s * std::sin(last), cs = std::cos(last);
    const Float first = std::atan2(-s * (cs * r(o.j, o.k) - sn * r(o.i, o.k)),
                                   cs * r(o.j, o.j) - sn * r(o.i, o.j));
    Vec<Float, 3> a;
    a[o.i] = first;
    a[o.j] = mid;
    a[o.k] = last;
    Float dist = Float(0);
    for (int n = 0; n < 3; n++) {
      a[n] += turn * std::round((hint[n] - a[n]) / turn);
      dist += (a[n] - hint[n]) * (a[n] - hint[n]);
    }
    if (family == 0 || dist < bestDist) {
      best = a;
      bestDist = dist;
    }
  }
  return best;
}

} // namespace litestl::math
