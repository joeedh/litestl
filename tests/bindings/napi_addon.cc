/* The fixture as a Node addon: the N-API half of the conformance tests. */

#include "fixture.h"

#include "litestl/binding/napi/napi_runtime.h"

namespace {

napi_value Init(napi_env env, napi_value exports)
{
  litestl::alloc::PermanentGuard guard;
  auto *rt = new litestl::binding::napi::Runtime(env, fixture::fixtureManager());
  rt->installExports(exports);
  return exports;
}

} // namespace

NAPI_MODULE(binding_fixture, Init)
