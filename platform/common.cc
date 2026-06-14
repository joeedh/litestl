#include "platform/time.h"

#include <chrono>

namespace litestl::time {
uint64_t now_ns()
{
  using clock = std::chrono::steady_clock;
  auto d = clock::now().time_since_epoch();
  return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(d).count();
}
} // namespace litestl::time
