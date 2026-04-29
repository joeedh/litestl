#pragma once

#include "binding_base.h"
#include "binding_number.h"
#include "binding_utils.h"
#include "util/vector.h"

#include <format>
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
  Method &isNeverNull()
  {
    if (returnType->type == BindingType::Pointer) {
      // I suppose I could just const_cast here...
      Pointer *p = static_cast<Pointer *>(returnType->clone());
      delete returnType;
      p->isNonNull = true;
      returnType = p;
    } else {
      printf("%s", std::format("return type not a pointer in method {}", name).c_str());
      abort();
    }
    return *this;
  }

  Method &argIsNullable(string argName)
  {
    for (auto &param : params) {
      if (param.name == argName && param.type->type == BindingType::Pointer) {
        // I suppose I could just const_cast here...
        Pointer *p = static_cast<Pointer *>(param.type->clone());
        delete param.type;
        p->isNonNull = false;
        param.type = p;
        return *this;
      } else if (param.name == argName) {
        printf("%s",
               std::format("arg {} not a pointer in method {}", argName, name).c_str());
        abort();
      }
    }

    printf("%s", std::format("arg {} not found in method {}", argName, name).c_str());
    abort();
  }

  // we are passing argNames by value here on purpose
  // to support temporaries (i.e. st->addMethodArgNames("foo", {"a", "b"});
  template <int N> Method &setArgNames(Vector<const char *, N> argNames)
  {
    if (argNames.size() != params.size()) {
      printf(
          "%s\n",
          std::format(
              "number of argNames ({}) does not match number of args ({}) in method {}",
              argNames.size(),
              params.size(),
              name)
              .c_str());
      abort();
    }
    for (int i = 0; i < argNames.size(); i++) {
      params[i].name = argNames[i];
    }
    return *this;
  }

  virtual size_t getSize() const override
  {
    return 0;
  }
  virtual BindingBase *clone() const override
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
    for (auto &param : out) {
      // set all pointers to non-null
      if (param.type->type == BindingType::Pointer) {
        Pointer *p = static_cast<Pointer *>(param.type->clone());
        delete param.type;
        p->isNonNull = true;
        param.type = p;
      }
    }
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
    (out.append(MethodParam{string(std::format("arg{}", I + 1).c_str()),
                            Bind<std::remove_cv_t<arg_t<I>>>()}),
     ...);
  }

  template <size_t... I>
  static void
  invokeImpl(class_type *self, void **args, void *ret, std::index_sequence<I...>)
  {
    (void)args;
    (void)ret;
    // note: we have to use remove_cvref_t becuase c++ doesn't allow us
    // to take pointers of references.
    if constexpr (std::is_void_v<return_type>) {
      (self->*Mfp)(*static_cast<std::remove_cvref_t<arg_t<I>> *>(args[I])...);
    } else {
      *static_cast<return_type *>(ret) =
          (self->*Mfp)(*static_cast<std::remove_cvref_t<arg_t<I>> *>(args[I])...);
    }
  }
};

} // namespace litestl::binding::types

#define MARGS(...) litestl::util::Vector<const char *, 8>({__VA_ARGS__})
/**
 * To set argument names, do e.g. BIND_STRUCT_METHOD(st, foo, MARGS("arg1", "arg2"));
 */
#define BIND_STRUCT_METHOD(def, mname, ARGNAMES)                                         \
  def->_addMethod<&std::remove_reference_t<decltype(*(def)->type_null)>::mname>(#mname)  \
      ->setArgNames(ARGNAMES)

/** Note: see Struct.setMethodArgNames */
#define BIND_STRUCT_METHOD_SIG(def, mname, RET, ARGNAMES, ARGS)                          \
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
    _BSM_m->setArgNames(ARGNAMES);                                                       \
  } while (0)
