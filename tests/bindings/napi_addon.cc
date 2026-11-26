/* The fixture as a Node addon: the N-API half of the conformance tests. */

#include "fixture.h"

#include "../../binding/napi/napi_runtime.h"

namespace {

/* N-API only: probeScale(h, 1) with a JS exception already pending ("pending"), which
 * the foreign call must refuse and leave for JS to see. */
napi_value probeScaleWithPendingException(napi_env env, napi_callback_info info)
{
  size_t argc = 1;
  napi_value argv[1];
  napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
  uint32_t h = 0;
  napi_get_value_uint32(env, argv[0], &h);
  napi_throw_error(env, nullptr, "pending");
  fixture::probeScale(h, 1);
  return nullptr;
}

napi_value Init(napi_env env, napi_value exports)
{
  litestl::alloc::PermanentGuard guard;
  auto *rt = new litestl::binding::napi::Runtime(env, fixture::fixtureManager());
  rt->installExports(exports);
  napi_value fn;
  napi_create_function(
      env, nullptr, 0, probeScaleWithPendingException, nullptr, &fn);
  napi_set_named_property(env, exports, "probeScaleWithPendingException", fn);
  return exports;
}

} // namespace

NAPI_MODULE(binding_fixture, Init)
