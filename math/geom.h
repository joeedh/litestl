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

template <isMathVec T>
static bool aabbSphereIsect(const T &p, typename T::value_type r, const AABB<T> &aabb)
{
  float3 lp = p;
  float3 lmin = aabb.min;
  float3 lmax = aabb.max;
  float3 cent = (lmin + lmax) * 0.5;

  lp -= cent;
  lmin -= cent;
  lmax -= cent;
  r *= r;

  bool isect = pointInAABB(aabb, p);
  if (isect) {
    return true;
  }

  float3 rect[8];
  rect[0] = float3(lmin[0], lmin[1], lmin[2]);
  rect[1] = float3(lmin[0], lmax[1], lmin[2]);
  rect[2] = float3(lmax[0], lmax[1], lmin[2]);
  rect[3] = float3(lmax[0], lmin[1], lmin[2]);
  rect[4] = float3(lmin[0], lmin[1], lmax[2]);
  rect[5] = float3(lmin[0], lmax[1], lmax[2]);
  rect[6] = float3(lmax[0], lmax[1], lmax[2]);
  rect[7] = float3(lmax[0], lmin[1], lmax[2]);
  for (int i = 0; i < 8; i++) {
    if (lp.distanceSqr(rect[i]) < r) {
      return true;
    }
  }
  float3 p2, p2arr, lparr, lminarr, lmaxarr;
  p2 = lp;
  p2arr = p2;
  lparr = lp;
  lminarr = lmin;
  lmaxarr = lmax;

  for (int i = 0; i < 3; i++) {
    p2 = lp;
    int i2 = ((i + 1) % 3);
    int i3 = ((i + 2) % 3);
    p2arr[i] = p2arr[i] < 0.0 ? lminarr[i] : lmaxarr[i];
    p2arr[i2] = std::min(std::max(p2arr[i2], lminarr[i2]), lmaxarr[i2]);
    p2arr[i3] = std::min(std::max(p2arr[i3], lminarr[i3]), lmaxarr[i3]);
    bool isect2 = p2.distanceSqr(lp) <= r;
    if (isect2) {
      return true;
    }
  }
  return false;
}

} // namespace litestl::math
