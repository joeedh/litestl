#pragma once
#include <tuple>

#include "binding_base.h"
#include "binding_types.h"
#include "litestl/util/Vector.h"
#include "litestl/util/string.h"

namespace litestl::binding::types {
using util::string;
using util::Vector;

struct MethodParam {
  string name;
  const BindingBase *type;
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
      return Bind((std::remove_cvref_t<return_type> *)nullptr);
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
                            Bind((std::remove_cv_t<arg_t<I>> *)nullptr)}),
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
// namespace litestl::binding::types