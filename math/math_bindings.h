#include "../binding/binding.h"
#include "vector.h"
#include <concepts>
#include <type_traits>

namespace litestl::binding {

template <typename T>
concept isMathVec = requires(T vec) { //
  typename T::is_math_vector;
};

template <isMathVec T> static BindingBase *Bind()
{
  int n = T::size;
  using value_type = typename T::value_type;
  string name = "unknown";

  if constexpr (std::is_same_v<value_type, float>) {
    name = "float";
  } else if constexpr (std::is_same_v<value_type, double>) {
    name = "double";
  } else if constexpr (std::is_same_v<value_type, int32_t>) {
    name = "int";
  } else if constexpr (std::is_same_v<value_type, int16_t>) {
    name = "short";
  } else if constexpr (std::is_same_v<value_type, int8_t>) {
    name = "byte";
  }

  // we have to do (n + '0') in this weird way to make clang happy
  char charN = (char)(n & 255) + '0';
  char size[2] = {charN, 0};
  name += string(size);

  types::Struct<T> *st = new types::Struct<T>(name, sizeof(T));
  st->add("vec", 0, new types::Array(Bind<typename T::value_type>(), n));
  return st;
}
} // namespace litestl::binding