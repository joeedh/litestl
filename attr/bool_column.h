#pragma once

/* BoolColumn: one byte per element (0 or 1) behind a bool API. Bit packing
 * is a later memory optimisation; bytes keep parallel page writes race-free
 * and paging identical to every other column. */

#include "attr/column.h"

namespace litestl::attr {

struct BoolOps {
  static constexpr MixPolicy default_policy = MixPolicy::CopyFirst;
  static constexpr bool supports(MixPolicy p)
  {
    return p == MixPolicy::Skip || p == MixPolicy::SetDefault || p == MixPolicy::CopyFirst ||
           p == MixPolicy::Custom;
  }
  static uint8_t combine(MixPolicy, const uint8_t *src, const double *, int)
  {
    return src[0];
  }
  static std::string key()
  {
    return "bool";
  }
};

using BoolColumn = Column<uint8_t, BoolOps>;

inline bool get_bool(const BoolColumn &c, uint32_t i)
{
  return c.get(i) != 0;
}
inline void set_bool(BoolColumn &c, uint32_t i, bool v)
{
  c.set(i, uint8_t(v ? 1 : 0));
}

} // namespace litestl::attr
