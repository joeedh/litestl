#pragma once

/* Stable type keys for column value types: "f32", "f64x3", "h128",
 * "f32[8]", ... Keys are part of the serialized format. */

#include <array>
#include <cstdint>
#include <string>

#include "math/vector.h"
#include "util/hash128.h"

namespace litestl::attr {

template <typename T> struct ValueKey; /* primary undefined: no key, no Column<T> */

#define LITESTL_ATTR_SCALAR_KEY(T, k)                                                            \
  template <> struct ValueKey<T> {                                                               \
    static std::string key()                                                                     \
    {                                                                                            \
      return k;                                                                                  \
    }                                                                                            \
  };
LITESTL_ATTR_SCALAR_KEY(int8_t, "i8")
LITESTL_ATTR_SCALAR_KEY(uint8_t, "u8")
LITESTL_ATTR_SCALAR_KEY(int16_t, "i16")
LITESTL_ATTR_SCALAR_KEY(uint16_t, "u16")
LITESTL_ATTR_SCALAR_KEY(int32_t, "i32")
LITESTL_ATTR_SCALAR_KEY(uint32_t, "u32")
LITESTL_ATTR_SCALAR_KEY(int64_t, "i64")
LITESTL_ATTR_SCALAR_KEY(uint64_t, "u64")
LITESTL_ATTR_SCALAR_KEY(float, "f32")
LITESTL_ATTR_SCALAR_KEY(double, "f64")
LITESTL_ATTR_SCALAR_KEY(hash::Hash128, "h128")
#undef LITESTL_ATTR_SCALAR_KEY

template <typename E, int N> struct ValueKey<math::Vec<E, N>> {
  static std::string key()
  {
    return ValueKey<E>::key() + "x" + std::to_string(N);
  }
};

template <typename E, size_t N> struct ValueKey<std::array<E, N>> {
  static std::string key()
  {
    return ValueKey<E>::key() + "[" + std::to_string(N) + "]";
  }
};

} // namespace litestl::attr
