#pragma once
#include <cstdlib>
#include <cstring>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
/* Provided by the JS loader (typescriptRuntime/wasiLoader.ts): writes the
 * current JS stack, NUL-terminated and at most maxLen bytes, to dst. */
extern "C" __attribute__((import_module("lstl"), import_name("stackTrace"))) void
lstl_js_stack_trace(char *dst, int maxLen);
#endif

namespace litestl::util::wasm {
static const char *getStackTrace(const char *prefix)
{
  char *tag = static_cast<char *>(malloc(2050));
  if (!tag) {
    return nullptr;
  }
  strcpy(tag, prefix);
  int start = strlen(prefix);
  char *tag2 = tag + start;

#ifdef __EMSCRIPTEN__
  EM_ASM(
      {
        Error.stackTraceLimit = 120;
        let error;
        try {
          throw new Error();
        } catch (err) {
          error = err;
          const s = "" + error.stack.split("\n").slice(4, -1).join("\n");
          const len = Math.min(s.length, 2000);
          for (let i = 0; i < len; i++) {
            HEAPU8[$0 + i] = s.charCodeAt(i);
          }
          HEAPU8[$0 + len] = 0;
        }
      },
      tag2);
#else
  lstl_js_stack_trace(tag2, 2000);
#endif

  return tag;
}
} // namespace litestl::util::wasm
