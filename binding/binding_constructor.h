#pragma once

#include "binding_base.h"
#include "binding_struct.h"
#include "binding_utils.h"
#include "util/vector.h"

#include <new>
#include <tuple>
#include <type_traits>
#include <utility>

namespace litestl::binding::types {
using util::string;
using util::Vector;

struct ConstructorParam {
  string name;
  const BindingBase *type;
};

using ConstructorThunk = void (*)(void *outBuf, void **args);

struct Constructor : public BindingBase {
  const BindingBase *ownerType = nullptr;
  Vector<ConstructorParam> params;
  ConstructorThunk thunk = nullptr;

  Constructor(string name) : BindingBase(BindingType::Constructor, name)
  {
  }
  Constructor(const Constructor &b)
      : BindingBase(b), ownerType(b.ownerType), params(b.params), thunk(b.thunk)
  {
  }
  virtual size_t getSize() const override
  {
    return 0;
  }
  virtual BindingBase *clone() override
  {
    return static_cast<BindingBase *>(new Constructor(*this));
  }
};

template <class CLS, class... Args> struct ConstructorBuilder {
  static constexpr size_t arity = sizeof...(Args);

  static void fillParams(Vector<ConstructorParam> &out)
  {
    fillParamsImpl(out, std::make_index_sequence<arity>{});
  }

  static void thunk(void *outBuf, void **args)
  {
    invokeImpl(outBuf, args, std::make_index_sequence<arity>{});
  }

private:
  template <size_t I> using arg_t = std::tuple_element_t<I, std::tuple<Args...>>;

  template <size_t... I>
  static void fillParamsImpl(Vector<ConstructorParam> &out, std::index_sequence<I...>)
  {
    (out.append(
         ConstructorParam{string(""), Bind<std::remove_cvref_t<arg_t<I>>>()}),
     ...);
  }

  template <size_t... I>
  static void invokeImpl(void *outBuf, void **args, std::index_sequence<I...>)
  {
    (void)args;
    ::new (outBuf)
        CLS(*static_cast<std::remove_cvref_t<arg_t<I>> *>(args[I])...);
  }
};

} // namespace litestl::binding::types

#define BIND_STRUCT_CONSTRUCTOR(def, name, ...)                                                \
  do {                                                                                   \
    using _BSC_C = std::remove_reference_t<decltype(*(def)->type_null)>;                 \
    using _BSC_CB =                                                                      \
        ::litestl::binding::types::ConstructorBuilder<_BSC_C, ##__VA_ARGS__>;            \
    auto *_BSC_c = new ::litestl::binding::types::Constructor(name);              \
    _BSC_c->ownerType = (def);                                                           \
    _BSC_CB::fillParams(_BSC_c->params);                                                 \
    _BSC_c->thunk = &_BSC_CB::thunk;                                                     \
    (def)->addConstructor(_BSC_c);                                                       \
  } while (0)

#define BIND_STRUCT_DEFAULT_CONSTRUCTOR(def) BIND_STRUCT_CONSTRUCTOR(def, "default")
