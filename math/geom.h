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
  float3 p2arr = lp;
  float3 lminarr = lmin;
  float3 lmaxarr = lmax;

  for (int i = 0; i < 3; i++) {
    p2arr = lp;
    int i2 = ((i + 1) % 3);
    int i3 = ((i + 2) % 3);
    p2arr[i] = p2arr[i] < 0.0 ? lminarr[i] : lmaxarr[i];
    p2arr[i2] = std::min(std::max(p2arr[i2], lminarr[i2]), lmaxarr[i2]);
    p2arr[i3] = std::min(std::max(p2arr[i3], lminarr[i3]), lmaxarr[i3]);
    bool isect2 = p2arr.distanceSqr(lp) <= r;
    if (isect2) {
      return true;
    }
  }
  return false;
}

/* Closest point on segment [a,b] to p. `t` returns the (optionally clamped)
 * parametric position along a->b. */
template <isMathVec T>
static T closestPointOnSegment(
    const T &p, const T &a, const T &b, bool clip, typename T::value_type &t_out)
{
  T ab = b - a;
  auto len2 = ab.dot(ab);
  auto t = len2 > 1e-17 ? (p - a).dot(ab) / len2 : typename T::value_type(0);

  if (clip) {
    t = std::min(std::max(t, typename T::value_type(0)), typename T::value_type(1));
  }

  t_out = t;
  return a + ab * t;
}

/* Closest point on triangle (a,b,c) to p. Ericson, "Real-Time Collision
 * Detection". */
template <isMathVec T>
static T closestPointOnTri(const T &p, const T &a, const T &b, const T &c)
{
  using S = typename T::value_type;
  T ab = b - a, ac = c - a, ap = p - a;
  S d1 = ab.dot(ap), d2 = ac.dot(ap);
  if (d1 <= 0 && d2 <= 0)
    return a;

  T bp = p - b;
  S d3 = ab.dot(bp), d4 = ac.dot(bp);
  if (d3 >= 0 && d4 <= d3)
    return b;

  S vc = d1 * d4 - d3 * d2;
  if (vc <= 0 && d1 >= 0 && d3 <= 0) {
    S v = d1 / (d1 - d3);
    return a + ab * v;
  }

  T cp = p - c;
  S d5 = ab.dot(cp), d6 = ac.dot(cp);
  if (d6 >= 0 && d5 <= d6)
    return c;

  S vb = d5 * d2 - d1 * d6;
  if (vb <= 0 && d2 >= 0 && d6 <= 0) {
    S w = d2 / (d2 - d6);
    return a + ac * w;
  }

  S va = d3 * d6 - d5 * d4;
  if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
    S w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
    return b + (c - b) * w;
  }

  S denom = S(1) / (va + vb + vc);
  S v = vb * denom, w = vc * denom;
  return a + ab * v + ac * w;
}

/* Cone test: a cone from `co` along `vector` (length = cone length), radius
 * `r1` at the base interpolating to `r2` at the tip. Mirrors the TS
 * aabb_cone_isect used by the JS BVH brush select. */
template <isMathVec T>
static bool aabbConeIsects(
    const T &co, const T &vector, typename T::value_type r1, typename T::value_type r2,
    const AABB<T> &aabb)
{
  using S = typename T::value_type;

  if (pointInAABB(aabb, co)) {
    return true;
  }

  S rlen = vector.length();
  T ray = vector;
  S radius2 = r2;

  if (rlen > S(0.00001)) {
    ray = ray * (S(1) / rlen);
    radius2 = r2 / rlen;
  }

  for (int axis = 0; axis < 3; axis++) {
    int a1 = (axis + 1) % 3;
    int a2 = (axis + 2) % 3;

    S amin = aabb.min[axis];
    S amax = aabb.max[axis];

    if (std::fabs(ray[axis]) <= S(0.0001)) {
      continue;
    }

    S t1 = (amin - co[axis]) / ray[axis];
    S t2 = (amax - co[axis]) / ray[axis];

    T p = co + ray * t1;
    S r = r1 + (radius2 - r1) * t1;

    if (t1 > 0 && t1 < rlen && p[a1] >= aabb.min[a1] - r && p[a1] <= aabb.max[a1] + r &&
        p[a2] >= aabb.min[a2] - r && p[a2] <= aabb.max[a2] + r)
    {
      return true;
    }

    p = co + ray * t2;
    if (t2 > 0 && t2 < rlen && p[a1] >= aabb.min[a1] && p[a1] <= aabb.max[a1] &&
        p[a2] >= aabb.min[a2] && p[a2] <= aabb.max[a2])
    {
      return true;
    }
  }

  return false;
}

/* Triangle-cone test (cone p1->p2, radius r1->r2). Mirrors TS tri_cone_isect. */
template <isMathVec T>
static bool triConeIsects(
    const T &p1, const T &p2, typename T::value_type r1, typename T::value_type r2,
    const T &v1, const T &v2, const T &v3)
{
  using S = typename T::value_type;
  T pco = closestPointOnTri(p1, v1, v2, v3);

  S t;
  T co = closestPointOnSegment(pco, p1, p2, false, t);

  S r = r1 + (r2 - r1) * t;
  return pco.distance(co) <= r;
}

/* A frustum is `n` plane equations [nx,ny,nz,d] (float4) with inward normals;
 * a point is inside when dot(n,p)+d >= 0 for every plane. */
static bool pointInFrustum(const float4 *planes, int n, const float3 &p)
{
  for (int i = 0; i < n; i++) {
    const float4 &pl = planes[i];
    if (pl[0] * p[0] + pl[1] * p[1] + pl[2] * p[2] + pl[3] < 0.0f) {
      return false;
    }
  }
  return true;
}

/* Conservative AABB-vs-frustum (positive-vertex method). */
static bool aabbFrustumIsects(const float4 *planes, int n, const AABB<float3> &aabb)
{
  for (int i = 0; i < n; i++) {
    const float4 &pl = planes[i];
    float px = pl[0] >= 0.0f ? aabb.max[0] : aabb.min[0];
    float py = pl[1] >= 0.0f ? aabb.max[1] : aabb.min[1];
    float pz = pl[2] >= 0.0f ? aabb.max[2] : aabb.min[2];

    if (pl[0] * px + pl[1] * py + pl[2] * pz + pl[3] < 0.0f) {
      return false;
    }
  }
  return true;
}

/* Conservative triangle-vs-frustum: rejected only if all three verts are
 * outside the same plane. */
static bool triFrustumIsects(
    const float4 *planes, int n, const float3 &v1, const float3 &v2, const float3 &v3)
{
  for (int i = 0; i < n; i++) {
    const float4 &pl = planes[i];
    float d1 = pl[0] * v1[0] + pl[1] * v1[1] + pl[2] * v1[2] + pl[3];
    float d2 = pl[0] * v2[0] + pl[1] * v2[1] + pl[2] * v2[2] + pl[3];
    float d3 = pl[0] * v3[0] + pl[1] * v3[1] + pl[2] * v3[2] + pl[3];

    if (d1 < 0.0f && d2 < 0.0f && d3 < 0.0f) {
      return false;
    }
  }
  return true;
}

} // namespace litestl::math
