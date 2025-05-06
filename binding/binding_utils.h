#pragma once
#include "binding_types.h"
#include "concepts"
#include "type_traits"
#include <cstdint>

namespace litestl::binding {

#ifdef _
#undef _
#endif

#define _(ctype, type, flags)                                                                \
  template <std::same_as<ctype> T> types::Number<ctype> *Bind()                              \
  {                                                                                      \
    return new types::Number<ctype>(NumberType::type, flags);                                   \
  }

_(signed char, Int8, NumberFlags::None);
_(unsigned char, Int8, NumberFlags::Unsigned);
_(short, Int16, NumberFlags::None);
_(unsigned short, Int16, NumberFlags::Unsigned);
_(int, Int32, NumberFlags::None);
_(unsigned int, Int32, NumberFlags::Unsigned);
_(int64_t, Int64, NumberFlags::None);
_(uint64_t, Int64, NumberFlags::Unsigned);

} // namespace litestl::binding