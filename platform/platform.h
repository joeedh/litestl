#pragma once

namespace litestl::platform {
void printStackTrace();
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
