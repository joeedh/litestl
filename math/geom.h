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

template <isMathVec T> static bool pointInAABB(const AABB<T> &aabb, const T &p)
{
  for (int i = 0; i < T::size; i++) {
    if (p[i] < aabb.min[i] || p[i] >= aabb.max[i]) {
      return false;
    }
  }
  return true;
}

template <isMathVec T>
static bool aabbRayIsects(const AABB<T> &aabb, const T &co, const T &indir)
{
  if (pointInAABB(aabb, co)) {
    return true;
  }

  for (int axis = 0; axis < 3; axis++) {
    T p;
    double t1, t2;

    int a1 = ((axis + 1) % 3);
    int a2 = ((axis + 2) % 3);

    double amin = aabb.min[axis];
    double amax = aabb.max[axis];

    if (std::fabs(indir[axis]) > 0.0001) {
      t1 = (amin - co[axis]) / indir[axis];
      t2 = (amax - co[axis]) / indir[axis];

      p = co + indir * t1;
    } else {
      continue;
    }

    if (p[a1] >= aabb.min[a1] && p[a1] <= aabb.max[a1] && p[a2] >= aabb.min[a2] &&
        p[a2] <= aabb.max[a2])
    {
      return true;
    }

    p = co + indir * t2;

    if (p[a1] >= aabb.min[a1] && p[a1] <= aabb.max[a1] && p[a2] >= aabb.min[a2] &&
        p[a2] <= aabb.max[a2])
    {
      return true;
    }
  }

  return false;
}

template <isMathVec T> struct RayTriIsect {
  typename T::value_type t;
  Vec<typename T::value_type, 2> uv;
};

/**
 * Ray-Triangle Intersection Test Routines          *
 * Different optimizations of my and Ben Trumbore's *
 * code from journals of graphics tools (JGT)       *
 * http://www.acm.org/jgt/                          *
 * by Tomas Moller, May 2000                        */
template <isMathVec T>
static bool rayTriIsect(const T &orig,
                        const T &dir,
                        const T &vert0,
                        const T &vert1,
                        const T &vert2,
                        RayTriIsect<T> &out)
{

  /* find vectors for two edges sharing vert0 */
  T edge1 = vert1 - vert0;
  T edge2 = vert2 - vert0;

  /* begin calculating determinant - also used to calculate U parameter */
  T pvec = dir.cross(edge2);

  /* if determinant is near zero, ray lies in plane of triangle */
  double det = edge1.dot(pvec);

  if (det > -0.000001 && det < 0.000001)
    return false;
  double inv_det = 1.0 / det;

  /* calculate distance from vert0 to ray origin */
  T tvec = orig - vert0;

  /* calculate U parameter and test bounds */
  double u = tvec.dot(pvec) * inv_det;
  if (u < 0.0 || u > 1.0)
    return false;

  /* prepare to test V parameter */
  T qvec = tvec.cross(edge1);

  /* calculate V parameter and test bounds */
  double v = dir.dot(qvec) * inv_det;
  if (v < 0.0 || u + v > 1.0)
    return false;

  /* calculate t, ray intersects triangle */
  double t = edge2.dot(qvec) * inv_det;

  out.uv[0] = 1.0 - u - v;
  out.uv[1] = u;
  out.t = t;
  return true;
}

} // namespace litestl::math
