#pragma once

#include <string>

namespace litestl::platform {
std::string getStackTrace();
static int debugBreak()
{
#ifdef WIN32
  __debugbreak();
#else
  char *p = nullptr;
  *p = 1;
#endif
  return 0;
}
}
