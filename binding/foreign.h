#pragma once

/*
 * Foreign dispatch: C++ calling a bound method descriptor on a host (JS) object, so
 * that JS can implement C++ interfaces. litestl doesn't know what the methods mean;
 * an interface layer (coredcc's ForeignDispatcher) maps its slots to descriptors.
 *
 * A host object is named by a Handle from the host's table (the TS runtime's
 * `foreignHandle(obj)`, or the N-API backend's). Handles are issued in increasing order
 * and never reused. A handle given to C++ carries one count, which C++ releases exactly
 * once with release(), on failure paths too.
 *
 * Hosts:
 * - Emscripten: EM_JS functions calling `Module.__lstlHooks`
 * (typescriptRuntime/foreign.ts).
 * - Other WASM (litestl's WASI build): the imports
 * `lstl.foreignCall`/`lstl.foreignRelease`.
 * - N-API: the runtime installs a Host for its env's thread (napi_foreign.cc).
 * - Otherwise there is none: call() fails and release() does nothing.
 *
 * See documentation/binding.md, "Foreign dispatch".
 */

#include "binding.h"
#include "../util/string.h"

#include <cstdint>

namespace litestl::binding::foreign {

using Handle = uint32_t;

/**
 * Calls `m` on host object `h`. `args` and `ret` use the thunk ABI (args[i] points at the
 * value, or at the pointer for pointer parameters; `ret` is uninitialized storage of the
 * return type, or null for void).
 *
 * On success a non-void `ret` has been constructed. On failure `ret` is left
 * unconstructed, `*error` holds the message, and false is returned.
 *
 * Refused before reaching the host: reference returns, ReturnPolicy::Owned returns, and
 * calls off the host's thread (it doesn't proxy: that thread may be waiting on the
 * caller).
 */
bool call(Handle h, const types::Method *m, void **args, void *ret, util::string *error);

/** Drops one count of `h`. A no-op when there is no host (teardown). */
void release(Handle h);

/** A host's entry points. `ctx` is passed back to both. */
struct Host {
  bool (*call)(void *ctx,
               Handle h,
               const types::Method *m,
               void **args,
               void *ret,
               util::string *error);
  void (*release)(void *ctx, Handle h);
  void *ctx;
};

/** Installs `host` for the calling thread (null restores the default). The host must
 * outlive its installation. */
void setHost(const Host *host);
const Host *host();

} // namespace litestl::binding::foreign
