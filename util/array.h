#pragma once
#include <array>

namespace litestl::util {
/* Alias rather than a direct `using std::array` so call sites read
 * `litestl::util::Array` and stay unaffected if we're later forced to
 * replace this with a hand-rolled fixed-size array. */
template <typename T, size_t N> using Array = std::array<T, N>;
} // namespace litestl::util
