#pragma once

#include "binding_base.h"
#include "binding_number.h"
#include "binding_utils.h"
#include "util/vector.h"

#include <tuple>
#include <type_traits>
#include <utility>

namespace litestl::binding::types {
using util::string;
using util::Vector;

struct MethodParam {
  string name;
  const BindingBase *type;
};

using MethodThunk = void (*)(void *self, void **args, void *ret);

struct Method : public BindingBase {
  const BindingBase *returnType = nullptr;
  Vector<MethodParam> params;
  MethodThunk thunk = nullptr;
  bool isConst = false;
  bool isStatic = false;

  Method(string name) : BindingBase(BindingType::Method, name)
  {
  }
  Method(const Method &b)
      : BindingBase(b), returnType(b.returnType), params(b.params), thunk(b.thunk),
        isConst(b.isConst), isStatic(b.isStatic)
  {
  }
  virtual size_t getSize() const override
  {
    return 0;
  }
  virtual BindingBase *clone() override
  {
    return static_cast<BindingBase *>(new Method(*this));
  }
};

namespace detail {
template <class T> struct MethodTraits;

template <class C, class R, class... A> struct MethodTraits<R (C::*)(A...)> {
  using class_type = C;
  using return_type = R;
  using args_tuple = std::tuple<A...>;
  static constexpr bool is_const = false;
  static constexpr size_t arity = sizeof...(A);
};

template <class C, class R, class... A> struct MethodTraits<R (C::*)(A...) const> {
  using class_type = C;
  using return_type = R;
  using args_tuple = std::tuple<A...>;
  static constexpr bool is_const = true;
  static constexpr size_t arity = sizeof...(A);
};
} // namespace detail

template <auto Mfp> struct MethodBuilder {
  using Traits = detail::MethodTraits<decltype(Mfp)>;
  using class_type = typename Traits::class_type;
  using return_type = typename Traits::return_type;
  static constexpr bool is_const = Traits::is_const;
  static constexpr size_t arity = Traits::arity;

  static const BindingBase *returnType()
  {
    if constexpr (std::is_void_v<return_type>) {
      return nullptr;
    } else {
      return Bind<std::remove_cvref_t<return_type>>();
    }
  }

  static void fillParams(Vector<MethodParam> &out)
  {
    fillParamsImpl(out, std::make_index_sequence<arity>{});
  }

  static void thunk(void *self, void **args, void *ret)
  {
    invokeImpl(
        static_cast<class_type *>(self), args, ret, std::make_index_sequence<arity>{});
  }

private:
  template <size_t I> using arg_t = std::tuple_element_t<I, typename Traits::args_tuple>;

  template <size_t... I>
  static void fillParamsImpl(Vector<MethodParam> &out, std::index_sequence<I...>)
  {
    (out.append(MethodParam{string(""), Bind<std::remove_cvref_t<arg_t<I>>>()}), ...);
  }

  template <size_t... I>
  static void
  invokeImpl(class_type *self, void **args, void *ret, std::index_sequence<I...>)
  {
    (void)args;
    (void)ret;
    if constexpr (std::is_void_v<return_type>) {
      (self->*Mfp)(*static_cast<std::remove_cvref_t<arg_t<I>> *>(args[I])...);
    } else {
      *static_cast<return_type *>(ret) =
          (self->*Mfp)(*static_cast<std::remove_cvref_t<arg_t<I>> *>(args[I])...);
    }
  }
};

} // namespace litestl::binding::types

#define BIND_STRUCT_METHOD(def, mname)                                                   \
  do {                                                                                   \
    using _BSM_C = std::remove_reference_t<decltype(*(def)->type_null)>;                 \
    using _BSM_MB = ::litestl::binding::types::MethodBuilder<&_BSM_C::mname>;            \
    auto *_BSM_m = new ::litestl::binding::types::Method(#mname);                        \
    _BSM_m->returnType = _BSM_MB::returnType();                                          \
    _BSM_MB::fillParams(_BSM_m->params);                                                 \
    _BSM_m->thunk = &_BSM_MB::thunk;                                                     \
    _BSM_m->isConst = _BSM_MB::is_const;                                                 \
    (def)->addMethod(_BSM_m);                                                            \
  } while (0)

#define BIND_STRUCT_METHOD_SIG(def, mname, RET, ARGS)                                    \
  do {                                                                                   \
    using _BSM_C = std::remove_reference_t<decltype(*(def)->type_null)>;                 \
    constexpr auto _BSM_FP = static_cast<RET(_BSM_C::*) ARGS>(&_BSM_C::mname);           \
    using _BSM_MB = ::litestl::binding::types::MethodBuilder<_BSM_FP>;                   \
    auto *_BSM_m = new ::litestl::binding::types::Method(#mname);                        \
    _BSM_m->returnType = _BSM_MB::returnType();                                          \
    _BSM_MB::fillParams(_BSM_m->params);                                                 \
    _BSM_m->thunk = &_BSM_MB::thunk;                                                     \
    _BSM_m->isConst = _BSM_MB::is_const;                                                 \
    (def)->addMethod(_BSM_m);                                                            \
  } while (0)
