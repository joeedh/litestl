#pragma once

#include "../util/compiler_util.h"
#include "aabb.h"
#include "tri_aabb_isect.h"
#include "vector.h"


namespace litestl::math {
enum _ShapeTests {
  Contains = 1,
  Intersects = 2,
  ContainedBy = 4,
};
MAKE_FLAGS_CLASS(ShapeTests, _ShapeTests, int);

template <isMathVec T> static T triNormal(const T &a, const T &b, const T &c)
{
  T t1 = b - a;
  T t2 = c - a;
  return t1.cross(t2).normalized();
}

template <isMathVec T> static bool aabbTriOverlaps(const AABB<T> &aabb, T &a, T &b, T &c)
{
  T center = aabb.center();
  T halfSize = aabb.halfSize();
  return detail::triBoxOverlap(center, halfSize, a, b, c);
}
} // namespace litestl::math
