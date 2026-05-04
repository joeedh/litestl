#pragma once
#include "binding_base.h"
#include "binding_types.h"
#include "concepts"
#include <cstdint>

namespace litestl::binding {

template <typename T>
  requires std::is_pointer_v<T>
static types::Pointer *Bind(std::remove_cv_t<T> *)
{
  using P = std::remove_cv_t<std::remove_pointer_t<T>>;
  return new types::Pointer(Bind((P *)(nullptr)));
}

template <typename T>
  requires std::is_reference_v<T>
static types::Reference *Bind(std::remove_reference_t<T> *)
{
  using R = std::remove_cv_t<std::remove_reference_t<T>>;
  return new types::Reference(Bind(reinterpret_cast<R *>(nullptr)));
}

#ifdef _
#undef _
#endif

#define _(ctype, type, flags)                                                            \
  static types::Number<ctype> *Bind(ctype *)                                             \
  {                                                                                      \
    return new types::Number<ctype>(NumberType::type, #ctype, flags);                    \
  }

_(signed char, Int8, NumberFlags::None);
_(unsigned char, Int8, NumberFlags::Unsigned);
_(short, Int16, NumberFlags::None);
_(unsigned short, Int16, NumberFlags::Unsigned);
_(int, Int32, NumberFlags::None);
_(unsigned int, Int32, NumberFlags::Unsigned);
_(int64_t, Int64, NumberFlags::None);
_(uint64_t, Int64, NumberFlags::Unsigned);
_(float, Float32, NumberFlags::None);
_(double, Float64, NumberFlags::None);

} // namespace litestl::binding
