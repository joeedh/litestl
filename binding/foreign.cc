#include "foreign.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/threading.h>
#endif

namespace litestl::binding::foreign {

namespace {

thread_local const Host *t_host = nullptr;

#if defined(__EMSCRIPTEN__)

/* The TS runtime fills Module.__lstlHooks (typescriptRuntime/foreign.ts). Written with
 * brackets so closure builds keep the property name. */
EM_JS(int,
      lstl_foreign_call,
      (uint32_t h, const void *m, void **args, void *ret, void *err),
      {
        var hooks = Module['__lstlHooks'];
        if (!hooks || !hooks['foreignCall']) {
          return -1;
        }
        return hooks['foreignCall'](h, m, args, ret, err);
      });

EM_JS(void, lstl_foreign_release, (uint32_t h), {
  var hooks = Module['__lstlHooks'];
  if (hooks && hooks['foreignRelease']) {
    hooks['foreignRelease'](h);
  }
});

#define LSTL_HAS_JS_HOST 1

#elif defined(__wasm__)

extern "C" {
__attribute__((import_module("lstl"), import_name("foreignCall"))) int
lstl_foreign_call(uint32_t h, const void *m, void **args, void *ret, void *err);
__attribute__((import_module("lstl"), import_name("foreignRelease"))) void
lstl_foreign_release(uint32_t h);
}

#define LSTL_HAS_JS_HOST 1

#endif

bool fail(util::string *error, const char *what, const types::Method *m)
{
  if (error) {
    *error = util::string(what) + " (" + m->getJsName() + ")";
  }
  return false;
}

} // namespace

void setHost(const Host *h)
{
  t_host = h;
}

const Host *host()
{
  return t_host;
}

bool call(Handle h, const types::Method *m, void **args, void *ret, util::string *error)
{
  if (m->returnType && m->returnType->type == BindingType::Reference) {
    return fail(error, "a foreign method can't return a reference", m);
  }
  if (m->returnPolicy == types::ReturnPolicy::Owned) {
    return fail(error, "a foreign method can't return an owned pointer", m);
  }
  if (t_host) {
    return t_host->call(t_host->ctx, h, m, args, ret, error);
  }
#ifdef LSTL_HAS_JS_HOST
#ifdef __EMSCRIPTEN__
  if (!emscripten_is_main_runtime_thread()) {
    return fail(error, "a foreign call off the main thread", m);
  }
#endif
  const int r = lstl_foreign_call(h, m, args, ret, error);
  if (r < 0) {
    return fail(error, "no foreign host: the TS runtime hasn't installed its hooks", m);
  }
  return r == 0;
#else
  (void)h;
  (void)args;
  (void)ret;
  return fail(error, "no foreign host", m);
#endif
}

void release(Handle h)
{
  if (t_host) {
    t_host->release(t_host->ctx, h);
    return;
  }
#ifdef LSTL_HAS_JS_HOST
#ifdef __EMSCRIPTEN__
  if (!emscripten_is_main_runtime_thread()) {
    return; /* no hooks on a worker; the count stays (documented as a leak) */
  }
#endif
  lstl_foreign_release(h);
#else
  (void)h;
#endif
}

} // namespace litestl::binding::foreign
