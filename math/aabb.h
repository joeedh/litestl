#include "vector.h"
#include <cfloat>

namespace litestl::math {
template <isMathVec T> struct AABB {
  T min;
  T max;

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
