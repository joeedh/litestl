#include "napi_runtime.h"

#include "litestl/binding/generators/typescript.h"
#include "litestl/util/alloc.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <new>
#include <string>

namespace litestl::binding::napi {

using types::_StructBase;
using types::Method;

namespace {

using NumberDesc = types::Number<char>;

/* Every Number<T> has the same layout (T is only a `using`), so subtype and
 * flags can be read through any instantiation. */
const NumberDesc *asNumber(const BindingBase *b)
{
  return reinterpret_cast<const NumberDesc *>(b);
}

bool isUnsigned(const NumberDesc *n)
{
  return (int(n->flags) & int(NumberFlags::Unsigned)) != 0;
}

bool nameIs(const string &name, const char *s)
{
  return strcmp(name.c_str(), s) == 0;
}

const _StructBase *asStruct(const BindingBase *b)
{
  return b && b->type == BindingType::Struct ? static_cast<const _StructBase *>(b) : nullptr;
}

const BindingBase *stripParentTemplate(const BindingBase *b)
{
  while (b && b->type == BindingType::ParentTemplParam) {
    b = static_cast<const types::ParentTemplateParam *>(b)->concreteType;
  }
  return b;
}

napi_valuetype typeOf(napi_env env, napi_value v)
{
  napi_valuetype t = napi_undefined;
  if (v) {
    napi_typeof(env, v, &t);
  }
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

napi_value undefinedValue(napi_env env)
{
  napi_value u;
  napi_get_undefined(env, &u);
  return u;
}

bool isArrayLike(napi_env env, napi_value v)
{
  bool is = false;
  napi_is_array(env, v, &is);
  if (!is) {
    napi_is_typedarray(env, v, &is);
  }
  return is;
}

uint32_t arrayLength(napi_env env, napi_value v)
{
  bool isTyped = false;
  napi_is_typedarray(env, v, &isTyped);
  if (isTyped) {
    size_t len = 0;
    napi_get_typedarray_info(env, v, nullptr, &len, nullptr, nullptr, nullptr);
    return uint32_t(len);
  }
  uint32_t len = 0;
  napi_get_array_length(env, v, &len);
  return len;
}

/** The typed array kind whose elements are laid out like `elem`, if any. */
bool typedArrayKind(const BindingBase *elem, napi_typedarray_type *out)
{
  if (elem->type != BindingType::Number) {
    return false;
  }
  const NumberDesc *n = asNumber(elem);
  const bool uns = isUnsigned(n);
  switch (n->subtype) {
  case NumberType::Int8:
    *out = uns ? napi_uint8_array : napi_int8_array;
    return true;
  case NumberType::Int16:
    *out = uns ? napi_uint16_array : napi_int16_array;
    return true;
  case NumberType::Int32:
    *out = uns ? napi_uint32_array : napi_int32_array;
    return true;
  case NumberType::Int64:
    *out = uns ? napi_biguint64_array : napi_bigint64_array;
    return true;
  case NumberType::Float32:
    *out = napi_float32_array;
    return true;
  case NumberType::Float64:
    *out = napi_float64_array;
    return true;
  }
  return false;
}

/* util::Vector and dynamic std::span both start {T *data; size_t size}. */
char *vectorData(void *vec)
{
  return *static_cast<char **>(vec);
}
size_t vectorSize(void *vec)
{
  return *reinterpret_cast<size_t *>(static_cast<char *>(vec) + sizeof(void *));
}

const types::Constructor *findCopyConstructor(const _StructBase *st)
{
  for (const types::Constructor *c : st->constructors) {
    if (c->params.size() == 1 && c->params[0].type->type == BindingType::Reference &&
        static_cast<const types::Reference *>(c->params[0].type)->refType->buildFullName() ==
            st->buildFullName())
    {
      return c;
    }
  }
  return nullptr;
}

void destroyOwned(const _StructBase *st, void *ptr)
{
  if (st->destructorThunk) {
    (*st->destructorThunk)(ptr);
  }
  alloc::release(ptr);
}

/** Per-call scratch: argument storage plus work to do after the thunk runs. */
struct Frame {
  std::deque<std::unique_ptr<uint64_t[]>> blocks;
  std::deque<std::string> strings;
  std::vector<std::function<void()>> after;

  void *alloc(size_t bytes)
  {
    blocks.emplace_back(new uint64_t[(bytes + 7) / 8 + 1]());
    return blocks.back().get();
  }
  ~Frame()
  {
    for (auto it = after.rbegin(); it != after.rend(); ++it) {
      (*it)();
    }
  }
};

struct MemberCtx {
  Runtime *rt;
  const types::StructMember *member;
};

struct CtorCtx {
  Runtime *rt;
  const _StructBase *st;
};

struct ViewCtx {
  Runtime *rt;
  const BindingBase *elem;
  size_t elemSize;
  void *base;
  uint32_t count;
  bool isVector;

  char *data() const
  {
    return isVector ? vectorData(base) : static_cast<char *>(base);
  }
  size_t length() const
  {
    return isVector ? vectorSize(base) : count;
  }
};

/* Builds a live, array-like Proxy from native length/get/set callbacks. */
const char *viewFactorySource = R"JS((function (len, get, set) {
  const index = (p) => {
    if (typeof p !== 'string') return -1;
    const i = Number(p);
    return Number.isInteger(i) && i >= 0 && String(i) === p ? i : -1;
  };
  return new Proxy([], {
    get(t, p) {
      if (p === 'length') return len();
      if (p === Symbol.iterator) {
        return function* () {
          const n = len();
          for (let i = 0; i < n; i++) yield get(i);
        };
      }
      const i = index(p);
      if (i >= 0) return get(i);
      return undefined;
    },
    set(t, p, v) {
      const i = index(p);
      if (i < 0) return false;
      set(i, v);
      return true;
    },
    has(t, p) {
      if (p === 'length') return true;
      const i = index(p);
      return i >= 0 && i < len();
    },
  });
}))JS";

bool writeNumber(napi_env env, napi_value v, const NumberDesc *num, void *dst)
{
  const napi_valuetype t = typeOf(env, v);
  if (num->subtype == NumberType::Int64) {
    if (t == napi_bigint) {
      bool lossless = false;
      if (isUnsigned(num)) {
        napi_get_value_bigint_uint64(env, v, static_cast<uint64_t *>(dst), &lossless);
      } else {
        napi_get_value_bigint_int64(env, v, static_cast<int64_t *>(dst), &lossless);
      }
      return true;
    }
    if (t != napi_number) {
      return false;
    }
    double d = 0.0;
    napi_get_value_double(env, v, &d);
    if (isUnsigned(num)) {
      *static_cast<uint64_t *>(dst) = uint64_t(d);
    } else {
      *static_cast<int64_t *>(dst) = int64_t(d);
    }
    return true;
  }
  if (t != napi_number) {
    return false;
  }
  double d = 0.0;
  napi_get_value_double(env, v, &d);
  const bool uns = isUnsigned(num);
  switch (num->subtype) {
  case NumberType::Int8:
    uns ? void(*static_cast<uint8_t *>(dst) = uint8_t(int64_t(d)))
        : void(*static_cast<int8_t *>(dst) = int8_t(int64_t(d)));
    break;
  case NumberType::Int16:
    uns ? void(*static_cast<uint16_t *>(dst) = uint16_t(int64_t(d)))
        : void(*static_cast<int16_t *>(dst) = int16_t(int64_t(d)));
    break;
  case NumberType::Int32:
    uns ? void(*static_cast<uint32_t *>(dst) = uint32_t(int64_t(d)))
        : void(*static_cast<int32_t *>(dst) = int32_t(int64_t(d)));
    break;
  case NumberType::Float32:
    *static_cast<float *>(dst) = float(d);
    break;
  case NumberType::Float64:
    *static_cast<double *>(dst) = d;
    break;
  case NumberType::Int64:
    break;
  }
  return true;
}

void writeEnum(const types::Enum *e, void *dst, int64_t v)
{
  switch (e->baseSize) {
  case 1:
    *static_cast<int8_t *>(dst) = int8_t(v);
    break;
  case 2:
    *static_cast<int16_t *>(dst) = int16_t(v);
    break;
  case 8:
    *static_cast<int64_t *>(dst) = v;
    break;
  default:
    *static_cast<int32_t *>(dst) = int32_t(v);
    break;
  }
}

int64_t readEnum(const types::Enum *e, const void *src)
{
  switch (e->baseSize) {
  case 1:
    return *static_cast<const int8_t *>(src);
  case 2:
    return *static_cast<const int16_t *>(src);
  case 8:
    return *static_cast<const int64_t *>(src);
  default:
    return *static_cast<const int32_t *>(src);
  }
}

constexpr size_t kMaxArgs = 32;

} // namespace

Runtime::Runtime(napi_env env, BindingManager *mgr) : env_(env), mgr_(mgr)
{
  for (const auto &group : mgr_->functionGroups()) {
    CallGroup *g = new CallGroup();
    g->rt = this;
    g->name = group[0]->qualifiedJsName().c_str();
    for (const Method *m : group) {
      g->methods.push_back(m);
      g->owners.push_back(nullptr);
      g->keys.push_back(dispatch::selectorKey(m).c_str());
    }
    g->mode = dispatch::mode(group);
    functionGroups_[g->name] = g;
  }
}

napi_value Runtime::throwType(const std::string &msg)
{
  napi_throw_type_error(env_, nullptr, msg.c_str());
  return nullptr;
}

const BindingBase *Runtime::lookup(const std::string &fullName) const
{
  string key(fullName.c_str());
  const auto &m = mgr_->getBindings();
  return m.contains(key) ? m.lookup(key) : nullptr;
}

Wrapped *Runtime::unwrap(napi_value v)
{
  if (!v || typeOf(env_, v) != napi_object) {
    return nullptr;
  }
  Wrapped *w = nullptr;
  if (napi_unwrap(env_, v, reinterpret_cast<void **>(&w)) != napi_ok) {
    return nullptr;
  }
  return w;
}

napi_value Runtime::makeView(const BindingBase *elem, void *base, uint32_t count, bool isVector)
{
  ViewCtx *ctx = new ViewCtx{this, elem, elem->getSize(), base, count, isVector};

  auto lenCb = [](napi_env env, napi_callback_info info) -> napi_value {
    void *data;
    napi_get_cb_info(env, info, nullptr, nullptr, nullptr, &data);
    napi_value out;
    napi_create_uint32(env, uint32_t(static_cast<ViewCtx *>(data)->length()), &out);
    return out;
  };
  auto getCb = [](napi_env env, napi_callback_info info) -> napi_value {
    size_t argc = 1;
    napi_value argv[1];
    void *data;
    napi_get_cb_info(env, info, &argc, argv, nullptr, &data);
    ViewCtx *c = static_cast<ViewCtx *>(data);
    uint32_t i = 0;
    napi_get_value_uint32(env, argv[0], &i);
    if (i >= c->length()) {
      return undefinedValue(env);
    }
    return c->rt->toJS(c->elem, c->data() + size_t(i) * c->elemSize);
  };
  auto setCb = [](napi_env env, napi_callback_info info) -> napi_value {
    size_t argc = 2;
    napi_value argv[2];
    void *data;
    napi_get_cb_info(env, info, &argc, argv, nullptr, &data);
    ViewCtx *c = static_cast<ViewCtx *>(data);
    uint32_t i = 0;
    napi_get_value_uint32(env, argv[0], &i);
    if (i >= c->length()) {
      napi_throw_range_error(env, nullptr, "index out of range");
      return nullptr;
    }
    c->rt->assign(c->elem, c->data() + size_t(i) * c->elemSize, argv[1]);
    return undefinedValue(env);
  };

  napi_value fns[3];
  napi_create_function(env_, "len", NAPI_AUTO_LENGTH, lenCb, ctx, &fns[0]);
  napi_create_function(env_, "get", NAPI_AUTO_LENGTH, getCb, ctx, &fns[1]);
  napi_create_function(env_, "set", NAPI_AUTO_LENGTH, setCb, ctx, &fns[2]);
  napi_add_finalizer(
      env_,
      fns[0],
      ctx,
      [](napi_env, void *data, void *) { delete static_cast<ViewCtx *>(data); },
      nullptr,
      nullptr);

  napi_value factory, undef = undefinedValue(env_), view;
  napi_get_reference_value(env_, viewFactory_, &factory);
  napi_call_function(env_, undef, factory, 3, fns, &view);
  return view;
}

napi_value Runtime::toJS(const BindingBase *binding, void *addr)
{
  binding = stripParentTemplate(binding);
  napi_value out;
  if (!binding || !addr) {
    return undefinedValue(env_);
  }

  switch (binding->type) {
  case BindingType::Number: {
    const NumberDesc *num = asNumber(binding);
    const bool uns = isUnsigned(num);
    switch (num->subtype) {
    case NumberType::Int8:
      uns ? napi_create_uint32(env_, *static_cast<uint8_t *>(addr), &out)
          : napi_create_int32(env_, *static_cast<int8_t *>(addr), &out);
      break;
    case NumberType::Int16:
      uns ? napi_create_uint32(env_, *static_cast<uint16_t *>(addr), &out)
          : napi_create_int32(env_, *static_cast<int16_t *>(addr), &out);
      break;
    case NumberType::Int32:
      uns ? napi_create_uint32(env_, *static_cast<uint32_t *>(addr), &out)
          : napi_create_int32(env_, *static_cast<int32_t *>(addr), &out);
      break;
    case NumberType::Int64:
      uns ? napi_create_bigint_uint64(env_, *static_cast<uint64_t *>(addr), &out)
          : napi_create_bigint_int64(env_, *static_cast<int64_t *>(addr), &out);
      break;
    case NumberType::Float32:
      napi_create_double(env_, *static_cast<float *>(addr), &out);
      break;
    case NumberType::Float64:
      napi_create_double(env_, *static_cast<double *>(addr), &out);
      break;
    }
    return out;
  }
  case BindingType::Boolean:
    napi_get_boolean(env_, *static_cast<bool *>(addr), &out);
    return out;
  case BindingType::Enum:
    napi_create_int64(
        env_, readEnum(static_cast<const types::Enum *>(binding), addr), &out);
    return out;
  case BindingType::Struct: {
    const _StructBase *st = static_cast<const _StructBase *>(binding);
    if (nameIs(st->name, "litestl::util::String")) {
      const util::string *s = static_cast<const util::string *>(addr);
      napi_create_string_utf8(env_, s->c_str(), s->size(), &out);
      return out;
    }
    if (nameIs(st->name, "litestl::util::CString")) {
      const char *s = *static_cast<const char **>(addr);
      if (!s) {
        return undefinedValue(env_);
      }
      napi_create_string_utf8(env_, s, NAPI_AUTO_LENGTH, &out);
      return out;
    }
    if (nameIs(st->name, "litestl::util::Vector") || nameIs(st->name, "litestl::util::Span")) {
      return makeView(stripParentTemplate(st->templateParams[0].type), addr, 0, true);
    }
    return wrap(st, addr, false);
  }
  case BindingType::Pointer:
  case BindingType::Reference: {
    void *target = *static_cast<void **>(addr);
    if (!target) {
      return undefinedValue(env_);
    }
    const BindingBase *inner = binding->type == BindingType::Pointer
                                   ? static_cast<const types::Pointer *>(binding)->ptrType
                                   : static_cast<const types::Reference *>(binding)->refType;
    return toJS(inner, target);
  }
  case BindingType::Array: {
    const auto *arr = reinterpret_cast<const types::Array<char> *>(binding);
    return makeView(
        stripParentTemplate(arr->arrayType), addr, uint32_t(arr->arraySize), false);
  }
  default:
    return undefinedValue(env_);
  }
}

bool Runtime::assign(const BindingBase *binding, void *addr, napi_value v)
{
  binding = stripParentTemplate(binding);
  switch (binding->type) {
  case BindingType::Number:
    if (!writeNumber(env_, v, asNumber(binding), addr)) {
      throwType("expected a number");
      return false;
    }
    return true;
  case BindingType::Boolean: {
    napi_value b;
    bool bv = false;
    napi_coerce_to_bool(env_, v, &b);
    napi_get_value_bool(env_, b, &bv);
    *static_cast<bool *>(addr) = bv;
    return true;
  }
  case BindingType::Enum: {
    int64_t e = 0;
    if (typeOf(env_, v) != napi_number) {
      throwType("expected an enum value");
      return false;
    }
    napi_get_value_int64(env_, v, &e);
    writeEnum(static_cast<const types::Enum *>(binding), addr, e);
    return true;
  }
  case BindingType::Pointer: {
    const types::Pointer *p = static_cast<const types::Pointer *>(binding);
    const napi_valuetype t = typeOf(env_, v);
    if (t == napi_null || t == napi_undefined) {
      *static_cast<void **>(addr) = nullptr;
      return true;
    }
    Wrapped *w = unwrap(v);
    const _StructBase *st = asStruct(p->ptrType);
    if (!w || !st || w->st->buildFullName() != st->buildFullName()) {
      throwType("expected a bound " + std::string(p->ptrType->buildFullName().c_str()));
      return false;
    }
    *static_cast<void **>(addr) = w->ptr;
    return true;
  }
  case BindingType::Struct: {
    const _StructBase *st = static_cast<const _StructBase *>(binding);
    if (nameIs(st->name, "litestl::util::String")) {
      if (typeOf(env_, v) != napi_string) {
        throwType("expected a string");
        return false;
      }
      *static_cast<util::string *>(addr) = util::string(jsString(env_, v));
      return true;
    }
    throwType("cannot assign to an embedded " + std::string(st->buildFullName().c_str()));
    return false;
  }
  default:
    throwType("cannot assign a value of this type");
    return false;
  }
}

napi_value Runtime::wrap(const _StructBase *st, void *ptr, bool owning)
{
  if (!ptr) {
    return undefinedValue(env_);
  }
  napi_value cls = getBoundClass(st);
  napi_value argv[2];
  napi_create_external(env_, ptr, nullptr, nullptr, &argv[0]);
  napi_get_boolean(env_, owning, &argv[1]);
  napi_value inst;
  napi_new_instance(env_, cls, 2, argv, &inst);
  return inst;
}

napi_value Runtime::getBoundClass(const _StructBase *st)
{
  std::string key(st->buildFullName().c_str());
  auto it = classRefs_.find(key);
  if (it != classRefs_.end()) {
    napi_value cls;
    napi_get_reference_value(env_, it->second, &cls);
    return cls;
  }

  std::vector<napi_property_descriptor> props;
  for (const auto &member : st->members) {
    napi_property_descriptor d = {};
    d.utf8name = member.name.c_str();
    d.getter = [](napi_env env, napi_callback_info info) -> napi_value {
      napi_value thisArg;
      void *data;
      napi_get_cb_info(env, info, nullptr, nullptr, &thisArg, &data);
      MemberCtx *ctx = static_cast<MemberCtx *>(data);
      Wrapped *w = ctx->rt->unwrap(thisArg);
      if (!w || !w->ptr) {
        napi_throw_error(env, nullptr, "bound object was disposed");
        return nullptr;
      }
      return ctx->rt->toJS(ctx->member->type, static_cast<char *>(w->ptr) + ctx->member->offset);
    };
    d.setter = [](napi_env env, napi_callback_info info) -> napi_value {
      size_t argc = 1;
      napi_value argv[1];
      napi_value thisArg;
      void *data;
      napi_get_cb_info(env, info, &argc, argv, &thisArg, &data);
      MemberCtx *ctx = static_cast<MemberCtx *>(data);
      Wrapped *w = ctx->rt->unwrap(thisArg);
      if (!w || !w->ptr) {
        napi_throw_error(env, nullptr, "bound object was disposed");
        return nullptr;
      }
      ctx->rt->assign(
          ctx->member->type, static_cast<char *>(w->ptr) + ctx->member->offset, argv[0]);
      return nullptr;
    };
    d.attributes = napi_property_attributes(napi_enumerable);
    d.data = new MemberCtx{this, &member};
    props.push_back(d);
  }

  // Instance methods. Template instantiations sharing a JS name dispatch like
  // free functions; plain overloads keep the first registered.
  std::vector<std::string> taken;
  for (const auto &member : st->members) {
    taken.push_back(member.name.c_str());
  }
  std::unordered_map<std::string, CallGroup *> groups;
  std::vector<std::string> order;
  for (const Method *m : st->methods) {
    if (m->isStatic) {
      continue;
    }
    std::string jsName = m->getJsName().c_str();
    auto it = groups.find(jsName);
    if (it == groups.end()) {
      if (std::find(taken.begin(), taken.end(), jsName) != taken.end()) {
        continue;
      }
      CallGroup *g = new CallGroup();
      g->rt = this;
      g->name = jsName;
      g->isInstance = true;
      groups[jsName] = g;
      order.push_back(jsName);
      it = groups.find(jsName);
    } else if (m->templateParams.size() == 0 ||
               it->second->methods[0]->templateParams.size() == 0)
    {
      continue;
    }
    it->second->methods.push_back(m);
    it->second->owners.push_back(st);
    it->second->keys.push_back(dispatch::selectorKey(m).c_str());
  }
  for (const std::string &name : order) {
    CallGroup *g = groups[name];
    util::Vector<const Method *> ms;
    for (const Method *m : g->methods) {
      ms.append(m);
    }
    g->mode = dispatch::mode(ms);
    napi_property_descriptor d = {};
    d.utf8name = g->name.c_str();
    d.method = [](napi_env env, napi_callback_info info) -> napi_value {
      size_t argc = kMaxArgs;
      napi_value argv[kMaxArgs];
      napi_value thisArg;
      void *data;
      napi_get_cb_info(env, info, &argc, argv, &thisArg, &data);
      CallGroup *g = static_cast<CallGroup *>(data);
      return g->rt->call(g, thisArg, argc, argv);
    };
    d.attributes = napi_default;
    d.data = g;
    props.push_back(d);
  }

  napi_value disposeSym;
  napi_get_reference_value(env_, disposeSymbol_, &disposeSym);
  if (disposeSym) {
    napi_property_descriptor d = {};
    d.name = disposeSym;
    d.method = [](napi_env env, napi_callback_info info) -> napi_value {
      napi_value thisArg;
      void *data;
      napi_get_cb_info(env, info, nullptr, nullptr, &thisArg, &data);
      Wrapped *w = static_cast<Runtime *>(data)->unwrap(thisArg);
      if (w && w->owning && w->ptr) {
        destroyOwned(w->st, w->ptr);
      }
      if (w) {
        w->ptr = nullptr;
        w->owning = false;
      }
      return nullptr;
    };
    d.attributes = napi_default;
    d.data = this;
    props.push_back(d);
  }

  auto ctorCb = [](napi_env env, napi_callback_info info) -> napi_value {
    size_t argc = 2;
    napi_value argv[2];
    napi_value thisArg;
    void *data;
    napi_get_cb_info(env, info, &argc, argv, &thisArg, &data);
    CtorCtx *ctx = static_cast<CtorCtx *>(data);
    void *ptr = nullptr;
    if (argc < 2 || typeOf(env, argv[0]) != napi_external) {
      napi_throw_type_error(env, nullptr, "bound classes are created with construct()");
      return nullptr;
    }
    napi_get_value_external(env, argv[0], &ptr);
    bool owning = false;
    napi_get_value_bool(env, argv[1], &owning);
    Wrapped *w = new Wrapped{ptr, ctx->st, owning};
    napi_wrap(
        env,
        thisArg,
        w,
        [](napi_env, void *data, void *) {
          Wrapped *w = static_cast<Wrapped *>(data);
          if (w->owning && w->ptr) {
            destroyOwned(w->st, w->ptr);
          }
          delete w;
        },
        nullptr,
        nullptr);
    return thisArg;
  };

  // JS identifiers can't hold "::" or "<>", but a class name is only cosmetic.
  std::string clsName;
  for (char c : key) {
    clsName += (isalnum(static_cast<unsigned char>(c)) ? c : '_');
  }

  napi_value cls;
  napi_define_class(env_,
                    clsName.c_str(),
                    NAPI_AUTO_LENGTH,
                    ctorCb,
                    new CtorCtx{this, st},
                    props.size(),
                    props.data(),
                    &cls);
  napi_ref ref;
  napi_create_reference(env_, cls, 1, &ref);
  classRefs_[key] = ref;
  return cls;
}

/* Converts one JS argument to the thunk ABI: the returned pointer is what
 * args[i] holds (see detail::thunkArg in binding_method.h). */
static void *marshal(Runtime *rt,
                     const BindingBase *pt,
                     napi_value a,
                     Frame &f,
                     const std::string &what);

static void *marshalStruct(Runtime *rt,
                           const _StructBase *st,
                           napi_value a,
                           Frame &f,
                           const std::string &what)
{
  napi_env env = rt->env();

  if (nameIs(st->name, "litestl::util::String")) {
    if (typeOf(env, a) != napi_string) {
      rt->throwType(what + ": expected a string");
      return nullptr;
    }
    void *buf = f.alloc(sizeof(util::string));
    new (buf) util::string(jsString(env, a));
    f.after.push_back([buf]() { std::destroy_at(static_cast<util::string *>(buf)); });
    return buf;
  }

  if (nameIs(st->name, "litestl::util::CString")) {
    if (typeOf(env, a) != napi_string) {
      rt->throwType(what + ": expected a string");
      return nullptr;
    }
    f.strings.push_back(jsString(env, a));
    void *buf = f.alloc(sizeof(char *));
    *static_cast<const char **>(buf) = f.strings.back().c_str();
    return buf;
  }

  if (nameIs(st->name, "litestl::util::Span")) {
    const BindingBase *elem = stripParentTemplate(st->templateParams[0].type);
    const bool readonly =
        st->templateParams.size() > 1 &&
        static_cast<const types::BoolLitType *>(st->templateParams[1].type)->data;
    if (!isArrayLike(env, a)) {
      rt->throwType(what + ": expected an array or typed array");
      return nullptr;
    }
    const size_t elemSize = elem->getSize();
    void *data = nullptr;
    size_t count = 0;

    napi_typedarray_type want;
    bool isTyped = false;
    napi_is_typedarray(env, a, &isTyped);
    if (isTyped && typedArrayKind(elem, &want)) {
      napi_typedarray_type have;
      size_t len;
      void *tdata;
      napi_get_typedarray_info(env, a, &have, &len, &tdata, nullptr, nullptr);
      if (have == want) {
        data = tdata;
        count = len;
      }
    }
    if (!data) {
      count = arrayLength(env, a);
      data = f.alloc(count * elemSize + 8);
      for (uint32_t i = 0; i < count; i++) {
        napi_value e;
        napi_get_element(env, a, i, &e);
        if (!rt->assign(elem, static_cast<char *>(data) + i * elemSize, e)) {
          return nullptr;
        }
      }
      if (!readonly) {
        napi_ref ref;
        napi_create_reference(env, a, 1, &ref);
        f.after.push_back([rt, ref, data, count, elem, elemSize]() {
          napi_env env = rt->env();
          napi_value arr;
          napi_get_reference_value(env, ref, &arr);
          for (uint32_t i = 0; i < count; i++) {
            napi_set_element(
                env, arr, i, rt->toJS(elem, static_cast<char *>(data) + i * elemSize));
          }
          napi_delete_reference(env, ref);
        });
      }
    }
    void *buf = f.alloc(sizeof(void *) * 2);
    *static_cast<void **>(buf) = data;
    *reinterpret_cast<size_t *>(static_cast<char *>(buf) + sizeof(void *)) = count;
    return buf;
  }

  if (nameIs(st->name, "litestl::util::Vector") && isArrayLike(env, a)) {
    const BindingBase *elem = stripParentTemplate(st->templateParams[0].type);
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
    if (!ctor || !resize) {
      rt->throwType(what + ": vector type cannot be built from an array");
      return nullptr;
    }
    void *vec = alloc::alloc("napi vector argument", st->getSize());
    ctor->thunk(vec, nullptr);
    f.after.push_back([st, vec]() { destroyOwned(st, vec); });
    int n = int(arrayLength(env, a));
    void *rargs[] = {&n};
    resize->thunk(vec, rargs, nullptr);
    const size_t elemSize = elem->getSize();
    for (int i = 0; i < n; i++) {
      napi_value e;
      napi_get_element(env, a, uint32_t(i), &e);
      if (!rt->assign(elem, vectorData(vec) + size_t(i) * elemSize, e)) {
        return nullptr;
      }
    }
    return vec;
  }

  Wrapped *w = rt->unwrap(a);
  if (!w || w->st->buildFullName() != st->buildFullName()) {
    rt->throwType(what + ": expected a bound " + std::string(st->buildFullName().c_str()));
    return nullptr;
  }
  if (!w->ptr) {
    napi_throw_error(env, nullptr, (what + ": bound object was disposed").c_str());
    return nullptr;
  }
  return w->ptr;
}

static void *marshal(Runtime *rt,
                     const BindingBase *pt,
                     napi_value a,
                     Frame &f,
                     const std::string &what)
{
  napi_env env = rt->env();
  pt = stripParentTemplate(pt);
  switch (pt->type) {
  case BindingType::Number:
  case BindingType::Boolean:
  case BindingType::Enum: {
    void *buf = f.alloc(8);
    bool ok = rt->assign(pt, buf, a);
    return ok ? buf : nullptr;
  }
  case BindingType::Struct:
    return marshalStruct(rt, static_cast<const _StructBase *>(pt), a, f, what);
  case BindingType::Reference: {
    const _StructBase *st =
        asStruct(stripParentTemplate(static_cast<const types::Reference *>(pt)->refType));
    if (!st) {
      rt->throwType(what + ": unsupported reference parameter");
      return nullptr;
    }
    return marshalStruct(rt, st, a, f, what);
  }
  case BindingType::Pointer: {
    const types::Pointer *p = static_cast<const types::Pointer *>(pt);
    void **slot = static_cast<void **>(f.alloc(sizeof(void *)));
    const napi_valuetype t = typeOf(env, a);
    if (t == napi_null || t == napi_undefined) {
      if (p->isNonNull) {
        rt->throwType(what + ": must not be null");
        return nullptr;
      }
      *slot = nullptr;
      return slot;
    }
    const _StructBase *st = asStruct(stripParentTemplate(p->ptrType));
    if (!st) {
      rt->throwType(what + ": unsupported pointer parameter");
      return nullptr;
    }
    *slot = marshalStruct(rt, st, a, f, what);
    return *slot ? slot : nullptr;
  }
  default:
    rt->throwType(what + ": unsupported parameter type");
    return nullptr;
  }
}

/** Converts a thunk's return buffer to JS, taking ownership of `ret`. */
static napi_value returnToJS(Runtime *rt, const Method *m, void *ret)
{
  napi_env env = rt->env();
  const BindingBase *rtType = stripParentTemplate(m->returnType);
  napi_value out = nullptr;

  if (rtType->type == BindingType::Struct) {
    const _StructBase *st = static_cast<const _StructBase *>(rtType);
    if (nameIs(st->name, "litestl::util::String") || nameIs(st->name, "litestl::util::CString"))
    {
      out = rt->toJS(st, ret);
      if (nameIs(st->name, "litestl::util::String")) {
        std::destroy_at(static_cast<util::string *>(ret));
      }
      alloc::release(ret);
      return out;
    }
    if (nameIs(st->name, "litestl::util::Vector")) {
      const BindingBase *elem = stripParentTemplate(st->templateParams[0].type);
      const size_t n = vectorSize(ret);
      const size_t elemSize = elem->getSize();
      char *data = vectorData(ret);
      if (nameIs(elem->name, "uint8")) {
        void *bytes;
        napi_value ab;
        napi_create_arraybuffer(env, n, &bytes, &ab);
        if (n) {
          memcpy(bytes, data, n);
        }
        napi_create_typedarray(env, napi_uint8_array, n, ab, 0, &out);
      } else {
        napi_create_array_with_length(env, n, &out);
        const _StructBase *elemSt = asStruct(elem);
        const types::Constructor *copy = elemSt ? findCopyConstructor(elemSt) : nullptr;
        for (size_t i = 0; i < n; i++) {
          void *addr = data + i * elemSize;
          napi_value e;
          if (elemSt && !nameIs(elemSt->name, "litestl::util::String")) {
            if (!copy) {
              napi_throw_error(env, nullptr, "vector element type has no copy constructor");
              destroyOwned(st, ret);
              return nullptr;
            }
            void *obj = alloc::alloc("napi vector element", elemSt->getSize());
            void *cargs[] = {addr};
            copy->thunk(obj, cargs);
            e = rt->wrap(elemSt, obj, true);
          } else {
            e = rt->toJS(elem, addr);
          }
          napi_set_element(env, out, uint32_t(i), e);
        }
      }
      destroyOwned(st, ret);
      return out;
    }
    // By-value struct: the caller owns the returned storage.
    return rt->wrap(st, ret, true);
  }

  if (rtType->type == BindingType::Pointer && m->returnPolicy == types::ReturnPolicy::Owned) {
    const _StructBase *st =
        asStruct(stripParentTemplate(static_cast<const types::Pointer *>(rtType)->ptrType));
    void *ptr = *static_cast<void **>(ret);
    alloc::release(ret);
    return st ? rt->wrap(st, ptr, true) : undefinedValue(env);
  }

  out = rt->toJS(rtType, ret);
  alloc::release(ret);
  return out;
}

napi_value Runtime::invoke(const Method *m, void *self, size_t argc, napi_value *argv)
{
  const size_t nparams = m->params.size();
  if (argc != nparams) {
    return throwType(std::string(m->getJsName().c_str()) + ": expected " +
                     std::to_string(nparams) + " arguments, got " + std::to_string(argc));
  }
  if (!m->thunk) {
    napi_throw_error(env_, nullptr, "method has no thunk");
    return nullptr;
  }

  napi_value result = nullptr;
  {
    Frame f;
    std::vector<void *> args(nparams + 1, nullptr);
    for (size_t i = 0; i < nparams; i++) {
      std::string what = std::string(m->getJsName().c_str()) + " argument '" +
                         m->params[int(i)].name.c_str() + "'";
      args[i] = marshal(this, m->params[int(i)].type, argv[i], f, what);
      if (!args[i]) {
        return nullptr;
      }
    }

    if (!m->returnType) {
      m->thunk(self, args.data(), nullptr);
      result = undefinedValue(env_);
    } else {
      size_t size = std::max<size_t>(m->returnType->getSize(), sizeof(void *));
      void *ret = alloc::alloc("napi return value", size);
      memset(ret, 0, size);
      m->thunk(self, args.data(), ret);
      result = returnToJS(this, m, ret);
    }
  } // Frame runs post-call work (span copy-back, temporaries) here.
  return result;
}

napi_value Runtime::call(CallGroup *g, napi_value thisArg, size_t argc, napi_value *argv)
{
  void *self = nullptr;
  if (g->isInstance) {
    Wrapped *w = unwrap(thisArg);
    if (!w || !w->ptr) {
      napi_throw_error(env_, nullptr, "bound object was disposed");
      return nullptr;
    }
    self = w->ptr;
  }

  size_t pick = 0;
  switch (g->mode) {
  case dispatch::Mode::Single:
    break;
  case dispatch::Mode::Select: {
    if (argc < 1 || typeOf(env_, argv[0]) != napi_string) {
      return throwType(g->name + ": the first argument must be a template selector key");
    }
    std::string key = jsString(env_, argv[0]);
    pick = g->methods.size();
    for (size_t i = 0; i < g->keys.size(); i++) {
      if (g->keys[i] == key) {
        pick = i;
        break;
      }
    }
    if (pick == g->methods.size()) {
      return throwType(g->name + ": no instantiation for selector '" + key + "'");
    }
    argc--;
    argv++;
    break;
  }
  case dispatch::Mode::Infer: {
    pick = g->methods.size();
    for (size_t i = 0; i < g->methods.size() && pick == g->methods.size(); i++) {
      const Method *m = g->methods[i];
      if (m->params.size() != argc) {
        continue;
      }
      bool ok = true;
      for (int k = 0; k < m->params.size() && ok; k++) {
        const _StructBase *st = dispatch::paramStruct(m->params[k].type);
        if (!st || nameIs(st->name, "litestl::util::String") ||
            nameIs(st->name, "litestl::util::CString"))
        {
          continue;
        }
        Wrapped *w = unwrap(argv[k]);
        ok = w && w->st->buildFullName() == st->buildFullName();
      }
      if (ok) {
        pick = i;
      }
    }
    if (pick == g->methods.size()) {
      return throwType(g->name + ": no instantiation matches the arguments' types");
    }
    break;
  }
  }

  return invoke(g->methods[pick], self, argc, argv);
}

napi_value Runtime::construct(const types::Constructor *c, size_t argc, napi_value *argv)
{
  const _StructBase *st = static_cast<const _StructBase *>(c->ownerType);
  if (argc != c->params.size()) {
    return throwType("constructor '" + std::string(c->name.c_str()) + "' expects " +
                     std::to_string(c->params.size()) + " arguments, got " +
                     std::to_string(argc));
  }
  void *obj = alloc::alloc(st->name.c_str(), st->getSize());
  {
    Frame f;
    std::vector<void *> args(c->params.size() + 1, nullptr);
    for (size_t i = 0; i < c->params.size(); i++) {
      args[i] = marshal(this, c->params[int(i)].type, argv[i], f, "constructor argument");
      if (!args[i]) {
        alloc::release(obj);
        return nullptr;
      }
    }
    c->thunk(obj, args.data());
  }
  return wrap(st, obj, true);
}

napi_value Runtime::functionFor(CallGroup *g)
{
  napi_value fn;
  napi_create_function(
      env_,
      g->name.c_str(),
      NAPI_AUTO_LENGTH,
      [](napi_env env, napi_callback_info info) -> napi_value {
        size_t argc = kMaxArgs;
        napi_value argv[kMaxArgs];
        void *data;
        napi_get_cb_info(env, info, &argc, argv, nullptr, &data);
        CallGroup *g = static_cast<CallGroup *>(data);
        return g->rt->call(g, nullptr, argc, argv);
      },
      g,
      &fn);
  return fn;
}

CallGroup *Runtime::staticGroup(const std::string &structName, const std::string &jsName)
{
  std::string key = structName + "#" + jsName;
  auto it = staticGroups_.find(key);
  if (it != staticGroups_.end()) {
    return it->second;
  }
  CallGroup *g = new CallGroup();
  g->rt = this;
  g->name = structName + "." + jsName;
  util::Vector<const Method *> ms;
  for (const BindingBase *b : mgr_->getBindings().values()) {
    const _StructBase *st = asStruct(b);
    if (!st || !nameIs(st->name, structName.c_str())) {
      continue;
    }
    for (const Method *m : st->methods) {
      if (m->isStatic && nameIs(m->getJsName(), jsName.c_str())) {
        g->methods.push_back(m);
        g->owners.push_back(st);
        g->keys.push_back(dispatch::selectorKey(m, st).c_str());
        ms.append(m);
      }
    }
  }
  if (g->methods.empty()) {
    delete g;
    return nullptr;
  }
  g->mode = dispatch::mode(ms);
  staticGroups_[key] = g;
  return g;
}

void Runtime::installExports(napi_value exports)
{
  {
    string error;
    if (!mgr_->checkFunctions(&error)) {
      napi_throw_error(env_, nullptr, error.c_str());
      return;
    }
  }

  {
    napi_value src, factory;
    napi_create_string_utf8(env_, viewFactorySource, NAPI_AUTO_LENGTH, &src);
    napi_run_script(env_, src, &factory);
    napi_create_reference(env_, factory, 1, &viewFactory_);

    napi_value global, symbolCtor, disposeSym = nullptr;
    napi_get_global(env_, &global);
    napi_get_named_property(env_, global, "Symbol", &symbolCtor);
    napi_get_named_property(env_, symbolCtor, "dispose", &disposeSym);
    if (typeOf(env_, disposeSym) != napi_symbol) {
      // Older runtimes lack Symbol.dispose; mint one so [Symbol.dispose]
      // can still be polyfilled onto Symbol by the host.
      napi_create_symbol(env_, nullptr, &disposeSym);
      napi_set_named_property(env_, symbolCtor, "dispose", disposeSym);
    }
    napi_create_reference(env_, disposeSym, 1, &disposeSymbol_);
  }

  auto define = [&](const char *name, napi_callback cb) {
    napi_value fn;
    napi_create_function(env_, name, NAPI_AUTO_LENGTH, cb, this, &fn);
    napi_set_named_property(env_, exports, name, fn);
  };

  define("construct", [](napi_env env, napi_callback_info info) -> napi_value {
    size_t argc = kMaxArgs;
    napi_value argv[kMaxArgs];
    void *data;
    napi_get_cb_info(env, info, &argc, argv, nullptr, &data);
    Runtime *rt = static_cast<Runtime *>(data);
    if (argc < 1 || typeOf(env, argv[0]) != napi_string) {
      return rt->throwType("construct(typeName)");
    }
    const _StructBase *st = asStruct(rt->lookup(jsString(env, argv[0])));
    if (!st) {
      return rt->throwType("construct: unknown struct " + jsString(env, argv[0]));
    }
    for (const types::Constructor *c : st->constructors) {
      if (c->params.size() == 0) {
        return rt->construct(c, 0, nullptr);
      }
    }
    return rt->throwType("construct: " + jsString(env, argv[0]) + " has no default constructor");
  });

  define("constructWith", [](napi_env env, napi_callback_info info) -> napi_value {
    size_t argc = kMaxArgs;
    napi_value argv[kMaxArgs];
    void *data;
    napi_get_cb_info(env, info, &argc, argv, nullptr, &data);
    Runtime *rt = static_cast<Runtime *>(data);
    if (argc < 2 || typeOf(env, argv[0]) != napi_string || typeOf(env, argv[1]) != napi_string)
    {
      return rt->throwType("constructWith(typeName, ctorName, ...args)");
    }
    const _StructBase *st = asStruct(rt->lookup(jsString(env, argv[0])));
    if (!st) {
      return rt->throwType("constructWith: unknown struct " + jsString(env, argv[0]));
    }
    const types::Constructor *c = st->findConstructor(string(jsString(env, argv[1]).c_str()));
    if (!c) {
      return rt->throwType("constructWith: no constructor named " + jsString(env, argv[1]));
    }
    return rt->construct(c, argc - 2, argv + 2);
  });

  define("getFunction", [](napi_env env, napi_callback_info info) -> napi_value {
    size_t argc = 1;
    napi_value argv[1];
    void *data;
    napi_get_cb_info(env, info, &argc, argv, nullptr, &data);
    Runtime *rt = static_cast<Runtime *>(data);
    std::string name = argc ? jsString(env, argv[0]) : "";
    auto it = rt->functionGroups_.find(name);
    if (it == rt->functionGroups_.end()) {
      return rt->throwType("getFunction: unknown function " + name);
    }
    auto ref = rt->functionRefs_.find(name);
    napi_value fn;
    if (ref != rt->functionRefs_.end()) {
      napi_get_reference_value(env, ref->second, &fn);
      return fn;
    }
    fn = rt->functionFor(it->second);
    napi_ref r;
    napi_create_reference(env, fn, 1, &r);
    rt->functionRefs_[name] = r;
    return fn;
  });

  define("getStatic", [](napi_env env, napi_callback_info info) -> napi_value {
    size_t argc = 2;
    napi_value argv[2];
    void *data;
    napi_get_cb_info(env, info, &argc, argv, nullptr, &data);
    Runtime *rt = static_cast<Runtime *>(data);
    if (argc < 2) {
      return rt->throwType("getStatic(structName, name)");
    }
    CallGroup *g = rt->staticGroup(jsString(env, argv[0]), jsString(env, argv[1]));
    if (!g) {
      return rt->throwType("getStatic: no static " + jsString(env, argv[1]) + " on " +
                           jsString(env, argv[0]));
    }
    return rt->functionFor(g);
  });

  define("getClass", [](napi_env env, napi_callback_info info) -> napi_value {
    size_t argc = 1;
    napi_value argv[1];
    void *data;
    napi_get_cb_info(env, info, &argc, argv, nullptr, &data);
    Runtime *rt = static_cast<Runtime *>(data);
    const _StructBase *st = asStruct(rt->lookup(argc ? jsString(env, argv[0]) : ""));
    if (!st) {
      return rt->throwType("getClass: unknown struct");
    }
    return rt->getBoundClass(st);
  });

  define("typeName", [](napi_env env, napi_callback_info info) -> napi_value {
    size_t argc = 1;
    napi_value argv[1];
    void *data;
    napi_get_cb_info(env, info, &argc, argv, nullptr, &data);
    Wrapped *w = argc ? static_cast<Runtime *>(data)->unwrap(argv[0]) : nullptr;
    if (!w) {
      return undefinedValue(env);
    }
    napi_value out;
    string name = w->st->buildFullName();
    napi_create_string_utf8(env, name.c_str(), name.size(), &out);
    return out;
  });

  define("bindingKeys", [](napi_env env, napi_callback_info info) -> napi_value {
    void *data;
    napi_get_cb_info(env, info, nullptr, nullptr, nullptr, &data);
    Runtime *rt = static_cast<Runtime *>(data);
    napi_value arr;
    napi_create_array(env, &arr);
    uint32_t i = 0;
    for (const auto &key : rt->mgr_->getBindings().keys()) {
      napi_value s;
      napi_create_string_utf8(env, key.c_str(), key.size(), &s);
      napi_set_element(env, arr, i++, s);
    }
    return arr;
  });

  define("generateTypeScript", [](napi_env env, napi_callback_info info) -> napi_value {
    void *data;
    napi_get_cb_info(env, info, nullptr, nullptr, nullptr, &data);
    Runtime *rt = static_cast<Runtime *>(data);
    util::Vector<const BindingBase *> types;
    for (const BindingBase *b : rt->mgr_->getBindings().values()) {
      types.append(b);
    }
    auto *files = generators::generateTypescript(types);
    napi_value obj;
    napi_create_object(env, &obj);
    for (const auto &key : files->keys()) {
      const string &body = files->lookup(key);
      napi_value s;
      napi_create_string_utf8(env, body.c_str(), body.size(), &s);
      napi_set_named_property(env, obj, key.c_str(), s);
    }
    alloc::Delete(files);
    return obj;
  });

  define("getMemSize", [](napi_env env, napi_callback_info info) -> napi_value {
    size_t argc = 1;
    napi_value argv[1];
    napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
    bool includePermanent = false;
    if (argc) {
      napi_get_value_bool(env, argv[0], &includePermanent);
    }
    size_t size = alloc::getMemorySize();
    if (includePermanent) {
      size += alloc::getPermanentMemorySize();
    }
    napi_value out;
    napi_create_double(env, double(size), &out);
    return out;
  });

  napi_value kind;
  napi_create_string_utf8(env_, "napi", NAPI_AUTO_LENGTH, &kind);
  napi_set_named_property(env_, exports, "kind", kind);
}

} // namespace litestl::binding::napi
