#include <chrono>
#include <emscripten.h>
#include <string>
#include <thread>

#include "util/wasm.h"


// use std c++ for wasm
namespace litestl::time {
void sleep_ms(int len)
{
  std::this_thread::sleep_for(std::chrono::milliseconds((long long)len));
}
void sleep_ns(int len)
{
  std::this_thread::sleep_for(std::chrono::milliseconds((long long)len));
}
} // namespace litestl::time

namespace litestl::platform {
int cpu_core_count()
{
  return std::thread::hardware_concurrency();
}
int max_thread_count()
{
  return cpu_core_count() << 1;
}
std::string getStackTrace()
{
  const char *s = litestl::util::wasm::getStackTrace("");
  std::string result = s ? s : "";
  if (s) {
    free((void *)s);
  }
  return result;
}
} // namespace litestl::platform
