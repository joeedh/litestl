#pragma once
#include "binding_base.h"
#include "binding_bind.h"
#include "binding_types.h"
#include "concepts"
#include <cstdint>
#include <type_traits>

namespace litestl::binding {

#ifdef _
#undef _
#endif

// there are some weird bugs with how clang handles
// std::same_as and unsigned integers,
// so we do a lot of dynamic checking here

#define _(ctype, Name, type)                                                             \
  template <> struct Binder<ctype> {                                                     \
    static types::Number<ctype> *bind()                                                  \
    {                                                                                    \
      auto *num = new types::Number<ctype>(NumberType::type, (Name), NumberFlags::None); \
      if constexpr (std::is_integral_v<ctype> && std::is_unsigned_v<ctype>) {            \
        num->flags |= NumberFlags::Unsigned;                                             \
        num->name = "u" + num->name;                                                     \
      }                                                                                  \
      return num;                                                                        \
    }                                                                                    \
  }

// why do we need signed char and char? 
_(char, "int8", Int8);
_(signed char, "int8", Int8);
_(unsigned char, "int8", Int8);
_(short, "int16", Int16);
_(unsigned short, "int16", Int16);
_(int, "int32", Int32);
_(unsigned int, "int32", Int32);
_(int64_t, "int64", Int64);
_(uint64_t, "int64", Int64);
_(float, "float", Float32);
_(double, "double", Float64);

} // namespace litestl::binding
