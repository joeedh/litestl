/* Foreign dispatch for the N-API runtime (binding/foreign.h): C++ calling methods of
 * JS objects. Mirrors typescriptRuntime/foreign.ts, so both backends pass the same
 * conformance tests; see documentation/binding.md, "Foreign dispatch". */
#include "napi_runtime.h"

#include "litestl/binding/foreign.h"
#include "litestl/util/alloc.h"

#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <unordered_map>

namespace litestl::binding::napi {

using types::_StructBase;
using types::Method;

namespace {

const BindingBase *strip(const BindingBase *b)
{
  while (b && b->type == BindingType::ParentTemplParam) {
    b = static_cast<const types::ParentTemplateParam *>(b)->concreteType;
  }
  return b;
}

bool nameIs(const string &name, const char *s)
{
  return strcmp(name.c_str(), s) == 0;
}

napi_valuetype typeOf(napi_env env, napi_value v)
{
  napi_valuetype t = napi_undefined;
  napi_typeof(env, v, &t);
  return t;
}

std::string jsString(napi_env env, napi_value v)
{
  size_t len = 0;
  napi_get_value_string_utf8(env, v, nullptr, 0, &len);
  std::string s(len, '\0');
  napi_get_value_string_utf8(env, v, s.data(), len + 1, &len);
  return s;
}

bool isObjectLike(napi_env env, napi_value v)
{
  const napi_valuetype t = typeOf(env, v);
  return t == napi_object || t == napi_function;
}

napi_value
callMethod(napi_env env, napi_value obj, const char *name, size_t argc, napi_value *argv)
{
  napi_value fn, out = nullptr;
  napi_get_named_property(env, obj, name, &fn);
  napi_call_function(env, obj, fn, argc, argv, &out);
  return out;
}

/** A failed call's message; clears the pending exception. */
std::string takeException(napi_env env)
{
  napi_value e;
  if (napi_get_and_clear_last_exception(env, &e) != napi_ok || !e) {
    return "unknown error";
  }
  bool isError = false;
  napi_is_error(env, e, &isError);
  napi_value s = e;
  if (isError) {
    napi_get_named_property(env, e, "message", &s);
  }
  napi_value str;
  if (napi_coerce_to_string(env, s, &str) != napi_ok) {
    bool pending = false;
    napi_is_exception_pending(env, &pending);
    if (pending) {
      napi_get_and_clear_last_exception(env, &str);
    }
    return "a value that can't be converted to a string";
  }
  return jsString(env, str);
}

bool pendingException(napi_env env)
{
  bool pending = false;
  napi_is_exception_pending(env, &pending);
  return pending;
}

const types::Constructor *copyConstructor(const _StructBase *st)
{
  for (const types::Constructor *c : st->constructors) {
    if (c->params.size() == 1 && c->params[0].type->type == BindingType::Reference &&
        static_cast<const types::Reference *>(c->params[0].type)
                ->refType->buildFullName() == st->buildFullName())
    {
      return c;
    }
  }
  return nullptr;
}

} // namespace

class ForeignHost {
public:
  explicit ForeignHost(Runtime *rt) : rt_(rt), env_(rt->env())
  {
    napi_value src, map;
    napi_create_string_utf8(env_, "new WeakMap()", NAPI_AUTO_LENGTH, &src);
    napi_run_script(env_, src, &map);
    napi_create_reference(env_, map, 1, &byObject_);
    host_ = {&ForeignHost::callThunk, &ForeignHost::releaseThunk, this};
  }

  const foreign::Host *host() const
  {
    return &host_;
  }

  napi_value registerObject(napi_value obj)
  {
    if (!isObjectLike(env_, obj)) {
      return rt_->throwType("foreignHandle: expected an object");
    }
    napi_value map = ref(byObject_);
    napi_value known = callMethod(env_, map, "get", 1, &obj);
    if (typeOf(env_, known) == napi_number) {
      uint32_t h = 0;
      napi_get_value_uint32(env_, known, &h);
      auto it = byHandle_.find(h);
      if (it != byHandle_.end()) {
        it->second.count++;
        return number(h);
      }
    }
    const uint32_t h = next_++;
    napi_ref r;
    napi_create_reference(env_, obj, 1, &r);
    byHandle_[h] = {r, 1};
    napi_value args[2] = {obj, number(h)};
    callMethod(env_, map, "set", 2, args);
    return number(h);
  }

  size_t count() const
  {
    return byHandle_.size();
  }

  void setConverters(const std::string &typeName, napi_value conv)
  {
    auto old = converters_.find(typeName);
    if (old != converters_.end()) {
      for (napi_ref r : {old->second.arg, old->second.ret}) {
        if (r) {
          napi_delete_reference(env_, r);
        }
      }
      converters_.erase(old);
    }
    Converters c;
    if (isObjectLike(env_, conv)) {
      napi_value fn;
      napi_get_named_property(env_, conv, "arg", &fn);
      if (typeOf(env_, fn) == napi_function) {
        napi_create_reference(env_, fn, 1, &c.arg);
      }
      napi_get_named_property(env_, conv, "ret", &fn);
      if (typeOf(env_, fn) == napi_function) {
        napi_create_reference(env_, fn, 1, &c.ret);
      }
    }
    if (c.arg || c.ret) {
      converters_[typeName] = c;
    }
  }

private:
  struct Entry {
    napi_ref obj;
    uint32_t count;
  };
  struct Converters {
    napi_ref arg = nullptr;
    napi_ref ret = nullptr;
  };
  /** Ends the call scope however the call leaves. */
  struct ScopeGuard {
    std::shared_ptr<CallScope> scope = std::make_shared<CallScope>();
    ~ScopeGuard()
    {
      scope->ended = true;
    }
  };

  static bool callThunk(void *ctx,
                        foreign::Handle h,
                        const Method *m,
                        void **args,
                        void *ret,
                        util::string *error)
  {
    return static_cast<ForeignHost *>(ctx)->call(h, m, args, ret, error);
  }
  static void releaseThunk(void *ctx, foreign::Handle h)
  {
    static_cast<ForeignHost *>(ctx)->release(h);
  }

  napi_value ref(napi_ref r) const
  {
    napi_value v;
    napi_get_reference_value(env_, r, &v);
    return v;
  }
  napi_value number(uint32_t v) const
  {
    napi_value out;
    napi_create_uint32(env_, v, &out);
    return out;
  }
  /* Failing steps set error_ and return false (or nullptr); the call reports it. */
  bool failWith(const std::string &msg)
  {
    error_ = msg;
    return false;
  }
  /** False, with error_ set, when JS left an exception pending. */
  bool check()
  {
    return !pendingException(env_) || failWith(takeException(env_));
  }

  void release(foreign::Handle h)
  {
    auto it = byHandle_.find(h);
    if (it == byHandle_.end() || --it->second.count > 0) {
      return;
    }
    napi_handle_scope hs;
    napi_open_handle_scope(env_, &hs);
    napi_value obj = ref(it->second.obj);
    callMethod(env_, ref(byObject_), "delete", 1, &obj);
    napi_delete_reference(env_, it->second.obj);
    byHandle_.erase(it);
    napi_close_handle_scope(env_, hs);
  }

  bool
  call(foreign::Handle h, const Method *m, void **args, void *ret, util::string *error)
  {
    const std::string name = m->getJsName().c_str();
    auto fail = [&](const std::string &msg) {
      if (error) {
        const bool prefixed = msg.rfind(name + ":", 0) == 0;
        *error = util::string((prefixed ? msg : name + ": " + msg).c_str());
      }
      return false;
    };
    auto it = byHandle_.find(h);
    if (it == byHandle_.end()) {
      return fail("unknown foreign handle " + std::to_string(h));
    }

    napi_handle_scope hs;
    napi_open_handle_scope(env_, &hs);
    error_.clear();
    const bool ok = dispatch(ref(it->second.obj), name, m, args, ret);
    napi_close_handle_scope(env_, hs);
    return ok || fail(error_);
  }

  bool dispatch(
      napi_value obj, const std::string &name, const Method *m, void **args, void *ret)
  {
    napi_value fn;
    napi_get_named_property(env_, obj, name.c_str(), &fn);
    if (!check()) {
      return false;
    }
    if (typeOf(env_, fn) != napi_function || inheritedFromObject(name, fn)) {
      return failWith("not implemented");
    }

    ScopeGuard guard;
    std::vector<napi_value> argv;
    for (int i = 0; i < m->params.size(); i++) {
      napi_value a = readArg(m->params[i].type, args[i], guard.scope);
      if (!a) {
        return false;
      }
      argv.push_back(a);
    }
    napi_value v = nullptr;
    napi_call_function(env_, obj, fn, argv.size(), argv.data(), &v);
    if (!check()) {
      return false;
    }
    if (isObjectLike(env_, v)) {
      napi_value then;
      napi_get_named_property(env_, v, "then", &then);
      if (!check()) {
        return false;
      }
      if (typeOf(env_, then) == napi_function) {
        return failWith("returned a Promise: foreign methods are synchronous");
      }
    }
    if (!m->returnType) {
      return true;
    }
    const BindingBase *rt = strip(m->returnType);
    if (rt->type == BindingType::Struct) {
      v = convert(rt->buildFullName().c_str(), false, v);
      if (!v) {
        return false;
      }
    }
    return writeReturn(m->returnType, ret, v, name + " return value");
  }

  bool inheritedFromObject(const std::string &name, napi_value fn)
  {
    napi_value global, objectCtor, proto, inherited;
    napi_get_global(env_, &global);
    napi_get_named_property(env_, global, "Object", &objectCtor);
    napi_get_named_property(env_, objectCtor, "prototype", &proto);
    napi_get_named_property(env_, proto, name.c_str(), &inherited);
    bool same = false;
    napi_strict_equals(env_, fn, inherited, &same);
    return same;
  }

  napi_value convert(const std::string &typeName, bool arg, napi_value v)
  {
    auto it = converters_.find(typeName);
    napi_ref fnRef =
        it == converters_.end() ? nullptr : (arg ? it->second.arg : it->second.ret);
    if (!fnRef) {
      return v;
    }
    napi_value undef, out = nullptr;
    napi_get_undefined(env_, &undef);
    napi_call_function(env_, undef, ref(fnRef), 1, &v, &out);
    return check() ? out : nullptr;
  }

  /* ---- arguments: a JS value for the C++ argument at `addr` (thunk ABI) ---- */

  napi_value
  readArg(const BindingBase *type, void *addr, const std::shared_ptr<CallScope> &scope)
  {
    const BindingBase *t = strip(type);
    switch (t->type) {
    case BindingType::Number:
    case BindingType::Boolean:
    case BindingType::Enum:
      return rt_->toJS(t, addr);
    case BindingType::Struct:
      return structArg(static_cast<const _StructBase *>(t), addr, false, scope);
    case BindingType::Reference: {
      const auto *r = static_cast<const types::Reference *>(t);
      const BindingBase *inner = strip(r->refType);
      return inner->type == BindingType::Struct
                 ? structArg(
                       static_cast<const _StructBase *>(inner), addr, r->isConst, scope)
                 : rt_->toJS(inner, addr);
    }
    case BindingType::Pointer: {
      const auto *p = static_cast<const types::Pointer *>(t);
      void *target = *static_cast<void **>(addr);
      if (!target) {
        napi_value null;
        napi_get_null(env_, &null);
        return null;
      }
      const BindingBase *inner = strip(p->ptrType);
      if (inner->type == BindingType::Struct) {
        return structArg(
            static_cast<const _StructBase *>(inner), target, p->isConst, scope);
      }
      break;
    }
    default:
      break;
    }
    failWith("unsupported parameter type " + std::string(t->buildFullName().c_str()));
    return nullptr;
  }

  napi_value structArg(const _StructBase *st,
                       void *addr,
                       bool ro,
                       const std::shared_ptr<CallScope> &scope)
  {
    if (nameIs(st->name, "litestl::util::String") ||
        nameIs(st->name, "litestl::util::CString"))
    {
      return rt_->toJS(st, addr);
    }
    if (nameIs(st->name, "litestl::util::Vector") ||
        nameIs(st->name, "litestl::util::Span"))
    {
      /* Copied: the container may not outlive the call. */
      const BindingBase *elem = strip(st->templateParams[0].type);
      char *data = *static_cast<char **>(addr);
      const size_t n =
          *reinterpret_cast<size_t *>(static_cast<char *>(addr) + sizeof(void *));
      napi_value out;
      if (nameIs(elem->name, "uint8")) {
        void *bytes;
        napi_value ab;
        napi_create_arraybuffer(env_, n, &bytes, &ab);
        if (n) {
          memcpy(bytes, data, n);
        }
        napi_create_typedarray(env_, napi_uint8_array, n, ab, 0, &out);
        return out;
      }
      napi_create_array_with_length(env_, n, &out);
      for (size_t i = 0; i < n; i++) {
        void *at = data + i * elem->getSize();
        napi_value e =
            elem->type == BindingType::Struct
                ? structArg(static_cast<const _StructBase *>(elem), at, ro, scope)
                : rt_->toJS(elem, at);
        if (!e) {
          return nullptr;
        }
        napi_set_element(env_, out, uint32_t(i), e);
      }
      return out;
    }
    rt_->pendingScope = scope;
    rt_->pendingReadonly = ro;
    napi_value w = rt_->wrap(st, addr, false);
    rt_->pendingScope.reset();
    rt_->pendingReadonly = false;
    return convert(st->buildFullName().c_str(), true, w);
  }

  /* ---- returns: constructs the value of type `type` in the uninitialized `ret` ---- */

  /** The pointer behind bound object `v` of type `pointee`, or nullptr (error_ set). */
  void *boundPtr(const BindingBase *pointee, napi_value v, const std::string &label)
  {
    Wrapped *w = rt_->unwrap(v);
    const _StructBase *st = pointee->type == BindingType::Struct
                                ? static_cast<const _StructBase *>(pointee)
                                : nullptr;
    if (!st || !w || w->st->buildFullName() != st->buildFullName()) {
      failWith(label + ": expected a bound " +
               std::string(pointee->buildFullName().c_str()));
      return nullptr;
    }
    if (!w->ptr) {
      failWith(label + ": the bound object was disposed");
    }
    return w->ptr;
  }

  bool
  writeReturn(const BindingBase *type, void *ret, napi_value v, const std::string &label)
  {
    const BindingBase *t = strip(type);
    switch (t->type) {
    case BindingType::Number:
    case BindingType::Boolean:
    case BindingType::Enum:
      return rt_->assign(t, ret, v) || failWith(label + ": " + takeException(env_));
    case BindingType::Pointer: {
      const auto *p = static_cast<const types::Pointer *>(t);
      const napi_valuetype vt = typeOf(env_, v);
      if (vt == napi_null || vt == napi_undefined) {
        if (p->isNonNull) {
          return failWith(label + ": must not be null");
        }
        *static_cast<void **>(ret) = nullptr;
        return true;
      }
      void *target = boundPtr(strip(p->ptrType), v, label);
      *static_cast<void **>(ret) = target;
      return target != nullptr;
    }
    case BindingType::Struct:
      return writeStruct(static_cast<const _StructBase *>(t), ret, v, label);
    default:
      break;
    }
    return failWith(label + ": unsupported return type " +
                    std::string(t->buildFullName().c_str()));
  }

  bool
  writeStruct(const _StructBase *st, void *ret, napi_value v, const std::string &label)
  {
    if (nameIs(st->name, "litestl::util::String")) {
      if (typeOf(env_, v) != napi_string) {
        return failWith(label + ": expected a string");
      }
      new (ret) util::string(jsString(env_, v).c_str());
      return true;
    }
    if (nameIs(st->name, "litestl::util::Vector")) {
      return writeVector(st, ret, v, label);
    }
    const types::Constructor *copy = copyConstructor(st);
    if (!copy) {
      return failWith(label + ": " + std::string(st->buildFullName().c_str()) +
                      " has no copy constructor");
    }
    void *src = boundPtr(st, v, label);
    if (!src) {
      return false;
    }
    void *cargs[2] = {src, nullptr};
    copy->thunk(ret, cargs);
    return true;
  }

  bool
  writeVector(const _StructBase *st, void *ret, napi_value v, const std::string &label)
  {
    const BindingBase *elem = strip(st->templateParams[0].type);
    const bool scalar = elem->type == BindingType::Number ||
                        elem->type == BindingType::Boolean ||
                        elem->type == BindingType::Enum;
    const types::Constructor *ctor = nullptr;
    for (const types::Constructor *c : st->constructors) {
      if (c->params.size() == 0) {
        ctor = c;
      }
    }
    const Method *resize = nullptr;
    for (const Method *m : st->methods) {
      if (nameIs(m->name, "resize")) {
        resize = m;
      }
    }
    if (!scalar || !ctor || !resize) {
      return failWith(label + ": " + std::string(st->buildFullName().c_str()) +
                      " can't be returned from JS");
    }
    bool isArray = false, isTyped = false;
    napi_is_array(env_, v, &isArray);
    napi_is_typedarray(env_, v, &isTyped);
    if (!isArray && !isTyped) {
      return failWith(label + ": expected an array or typed array");
    }
    napi_value lenV;
    napi_get_named_property(env_, v, "length", &lenV);
    uint32_t n = 0;
    napi_get_value_uint32(env_, lenV, &n);

    void *noArgs[1] = {nullptr};
    ctor->thunk(ret, noArgs);
    int size = int(n);
    void *rargs[2] = {&size, nullptr};
    resize->thunk(ret, rargs, nullptr);
    char *data = *static_cast<char **>(ret);
    for (uint32_t i = 0; i < n; i++) {
      napi_value e;
      napi_get_element(env_, v, i, &e);
      if (!rt_->assign(elem, data + i * elem->getSize(), e)) {
        failWith(label + ": " + takeException(env_));
        if (st->destructorThunk) {
          (*st->destructorThunk)(ret); /* leave nothing constructed */
        }
        return false;
      }
    }
    return true;
  }

  Runtime *rt_;
  napi_env env_;
  foreign::Host host_;
  napi_ref byObject_ = nullptr;
  std::unordered_map<uint32_t, Entry> byHandle_;
  std::unordered_map<std::string, Converters> converters_;
  uint32_t next_ = 1;
  std::string error_;
};

void Runtime::installForeign(napi_value exports)
{
  foreign_ = new ForeignHost(this);
  foreign::setHost(foreign_->host());
  /* The host is per thread and dies with the env (a worker's env is torn down first). */
  napi_add_env_cleanup_hook(
      env_,
      [](void *data) {
        auto *host = static_cast<ForeignHost *>(data);
        if (foreign::host() == host->host()) {
          foreign::setHost(nullptr);
        }
        delete host;
      },
      foreign_);

  auto define = [&](const char *name, napi_callback cb) {
    napi_value fn;
    napi_create_function(env_, name, NAPI_AUTO_LENGTH, cb, this, &fn);
    napi_set_named_property(env_, exports, name, fn);
  };
  define("foreignHandle", [](napi_env env, napi_callback_info info) -> napi_value {
    size_t argc = 1;
    napi_value argv[1] = {nullptr};
    void *data;
    napi_get_cb_info(env, info, &argc, argv, nullptr, &data);
    Runtime *rt = static_cast<Runtime *>(data);
    if (argc < 1) {
      return rt->throwType("foreignHandle: expected an object");
    }
    return rt->foreign_->registerObject(argv[0]);
  });
  define("foreignCount", [](napi_env env, napi_callback_info info) -> napi_value {
    void *data;
    napi_get_cb_info(env, info, nullptr, nullptr, nullptr, &data);
    napi_value out;
    napi_create_uint32(
        env, uint32_t(static_cast<Runtime *>(data)->foreign_->count()), &out);
    return out;
  });
  define("setForeignConverter", [](napi_env env, napi_callback_info info) -> napi_value {
    size_t argc = 2;
    napi_value argv[2] = {nullptr, nullptr};
    void *data;
    napi_get_cb_info(env, info, &argc, argv, nullptr, &data);
    Runtime *rt = static_cast<Runtime *>(data);
    if (argc < 2 || typeOf(env, argv[0]) != napi_string) {
      return rt->throwType("setForeignConverter(typeName, {arg, ret})");
    }
    rt->foreign_->setConverters(jsString(env, argv[0]), argv[1]);
    return nullptr;
  });
}

} // namespace litestl::binding::napi
