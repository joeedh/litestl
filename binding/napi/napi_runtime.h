#pragma once

/*
 * N-API runtime for the binding system: the native counterpart of
 * typescriptRuntime/, reading the descriptors directly instead of out of a
 * WASM heap. Pointers never cross into JS as numbers.
 *
 * An addon creates one Runtime per module init and installs its exports:
 *
 *   napi_value Init(napi_env env, napi_value exports) {
 *     auto *rt = new litestl::binding::napi::Runtime(env, getBindingManager());
 *     rt->installExports(exports);
 *     return exports;
 *   }
 *
 * The exports implement IBindingBackend from typescriptRuntime/backend.ts, the
 * interface the WASM runtime also implements, so generated TS works on both.
 */

#include <node_api.h>

#include "litestl/binding/binding_dispatch.h"
#include "litestl/binding/manager.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace litestl::binding::napi {

/** One foreign call (napi_foreign.cc): wrappers and views made for its arguments, and
 * values reached through them, stop working once it has ended. */
struct CallScope {
  bool ended = false;
};

/** The C++ object behind a bound JS object. */
struct Wrapped {
  void *ptr;
  const types::_StructBase *st;
  /** Owning wrappers destroy the object (destructor + alloc::release) on
   * [Symbol.dispose]() or garbage collection. */
  bool owning;
  /** Set for wrappers of a foreign call's arguments; unwrap() clears ptr after it. */
  std::shared_ptr<CallScope> scope = nullptr;
  /** A const argument: setters and non-const methods throw. */
  bool readonly = false;
};

class ForeignHost;

/** What one JS function dispatches among: a free function's or static
 * method's instantiations, or an instance method's. */
struct CallGroup {
  class Runtime *rt;
  std::string name;
  std::vector<const types::Method *> methods;
  /** Per method: the struct it belongs to, or nullptr for free functions. */
  std::vector<const types::_StructBase *> owners;
  /** Per method: selector key (select mode only). */
  std::vector<std::string> keys;
  dispatch::Mode mode = dispatch::Mode::Single;
  bool isInstance = false;
};

class Runtime {
public:
  Runtime(napi_env env, BindingManager *mgr);

  /** Installs construct / constructWith / getFunction / getStatic / getClass /
   * typeName / bindingKeys / generateTypeScript / getMemSize on `exports`. */
  void installExports(napi_value exports);

  /** A JS value for the C++ value of type `binding` stored at `addr`. Structs
   * become non-owning wrappers over that storage. */
  napi_value toJS(const BindingBase *binding, void *addr);
  /** A bound object for `ptr`, of struct type `st`. */
  napi_value wrap(const types::_StructBase *st, void *ptr, bool owning);
  /** The wrapper behind `v`, or nullptr when `v` is not a bound object. */
  Wrapped *unwrap(napi_value v);

  napi_env env() const
  {
    return env_;
  }
  BindingManager *manager() const
  {
    return mgr_;
  }

  /* Used by the static napi callbacks in napi_runtime.cc. */
  napi_value call(CallGroup *group, napi_value thisArg, size_t argc, napi_value *argv);
  napi_value invoke(const types::Method *m, void *self, size_t argc, napi_value *argv);
  napi_value construct(const types::Constructor *c, size_t argc, napi_value *argv);
  napi_value getBoundClass(const types::_StructBase *st);
  napi_value makeView(const BindingBase *elem, void *base, uint32_t count, bool isVector);
  bool assign(const BindingBase *binding, void *addr, napi_value v);
  napi_value functionFor(CallGroup *group);
  CallGroup *staticGroup(const std::string &structName, const std::string &jsName);
  const BindingBase *lookup(const std::string &fullName) const;
  napi_value throwType(const std::string &msg);

  /** While set, wrappers and views that toJS/wrap make join this call scope. */
  std::shared_ptr<CallScope> pendingScope;
  bool pendingReadonly = false;

private:
  /* napi_foreign.cc */
  void installForeign(napi_value exports);
  ForeignHost *foreign_ = nullptr;

  napi_env env_;
  BindingManager *mgr_;
  std::unordered_map<std::string, napi_ref> classRefs_;
  std::unordered_map<std::string, CallGroup *> functionGroups_;
  std::unordered_map<std::string, CallGroup *> staticGroups_;
  std::unordered_map<std::string, napi_ref> functionRefs_;
  napi_ref viewFactory_ = nullptr;
  napi_ref disposeSymbol_ = nullptr;
};

} // namespace litestl::binding::napi
