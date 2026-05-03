#pragma once

#include "vector.h"
#include <cfloat>

namespace litestl::math {
// default template parameter is to help intellisense
template <typename T = Vec<float, 3>>
concept isMathAABB = requires(T vec) { //
  typename T::is_math_aabb;
};

template <isMathVec T> struct AABB {
  using value_type = T;
  T min;
  T max;
  /** type tag */
  using is_math_aabb = int;

  AABB() : min(T::negative_limit), max(T::negative_limit)
  {
  }

  AABB(const T &min, const T &max) : min(min), max(max)
  {
  }
  AABB(T min, T max) : min(min), max(max)
  {
  }
  AABB(const AABB &aabb) = default;
  AABB &operator=(const AABB &aabb) = default;

  AABB &reset()
  {
    min = {T::positive_limit};
    max = {T::negative_limit};
    return *this;
  }

  bool isEmpty()
  {
    for (int i = 0; i < T::size; i++) {
      if (min[i] != T::positive_limit) {
        return false;
      }
      if (max[i] != T::negative_limit) {
        return false;
      }
    }
    return true;
  }

  inline T center() const
  {
    return (min + max) * 0.5f;
  }

  inline T halfSize() const
  {
    return (max - min) * 0.5f;
  }
  inline T size() const
  {
    return max - min;
  }

  inline AABB &add(const T &v)
  {
    min.min(v);
    max.max(v);
    return *this;
  }
};
} // namespace litestl::math
