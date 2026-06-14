#pragma once

#include <cstdint>

namespace litestl::time {
void sleep_ms(int ms);
void sleep_ns(int ns);

/* Monotonic nanosecond clock; epoch is unspecified, only differences are
 * meaningful. */
uint64_t now_ns();
} // namespace litestl::time
