#pragma once

#include "binding_base.h"
#include "binding_number.h"
#include "binding_utils.h"
#include "util/vector.h"

#include <cstdlib>
#include <format>
#include <new>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>

namespace litestl::binding::types {
using util::string;
using util::Vector;

/** A template parameter of a struct, function or method: the C++ parameter
 * name and the concrete type it was instantiated with. */
struct StructTemplate {
  string name;
  const BindingBase *type;
};

struct MethodParam {
  string name;
  const BindingBase *type;
};

using MethodThunk = void (*)(void *self, void **args, void *ret);

/** Who frees a pointer returned from a function or method. Owned pointers must
 * come from litestl::alloc (alloc::New); the JS wrapper destroys them. */
enum class ReturnPolicy : int32_t {
  Borrowed = 0,
  Owned = 1,
};

/**
 * A bound method, static method or free function. Free functions are
 * registered with the BindingManager under their full name, so for those
 * `name` carries the namespace (`"sculptcore::mesh::createCube"`).
 */
struct Method : public BindingBase {
  const BindingBase *returnType = nullptr;
  Vector<MethodParam> params;
  MethodThunk thunk = nullptr;
  bool isConst = false;
  bool isStatic = false;
  Vector<StructTemplate> templateParams;
  ReturnPolicy returnPolicy = ReturnPolicy::Borrowed;
  /** JS-visible name; empty means the last `::` segment of `name`. */
  string jsName;

  Method(string name) : BindingBase(BindingType::Method, name)
  {
  }
  Method(const Method &b)
      : BindingBase(b), returnType(b.returnType), params(b.params), thunk(b.thunk),
        isConst(b.isConst), isStatic(b.isStatic), templateParams(b.templateParams),
        returnPolicy(b.returnPolicy), jsName(b.jsName)
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

  /** The returned pointer was allocated with litestl::alloc and is handed to
   * the caller, whose JS wrapper destroys it on dispose (or GC, under N-API). */
  Method &returnsOwned()
  {
    if (!returnType || returnType->type != BindingType::Pointer) {
      printf("%s\n",
             std::format("returnsOwned: {} does not return a pointer", name).c_str());
      abort();
    }
    returnPolicy = ReturnPolicy::Owned;
    return *this;
  }
  Method &returnsBorrowed()
  {
    returnPolicy = ReturnPolicy::Borrowed;
    return *this;
  }
  /** Rename the function on the JS side, e.g. to give one C++ overload a
   * distinct name. */
  Method &setJsName(string n)
  {
    jsName = n;
    return *this;
  }
  Method &addTemplateParam(const BindingBase *paramType, string paramName)
  {
    templateParams.append({paramName, paramType});
    hasCachedName = false;
    return *this;
  }

  /** `name` with any namespace prefix removed. */
  string shortName() const
  {
    std::string s = name.c_str();
    auto i = s.rfind("::");
    return i == std::string::npos ? name : string(s.substr(i + 2).c_str());
  }
  /** The namespace part of `name` (empty when there is none). */
  string namespaceName() const
  {
    std::string s = name.c_str();
    auto i = s.rfind("::");
    return i == std::string::npos ? string("") : string(s.substr(0, i).c_str());
  }
  string getJsName() const
  {
    return jsName.size() > 0 ? jsName : shortName();
  }
  /** Namespace-qualified JS name; template instantiations share it. */
  string qualifiedJsName() const
  {
    string ns = namespaceName();
    return ns.size() > 0 ? ns + "::" + getJsName() : getJsName();
  }

  /** `name<template args>[param types]` -- unique per overload and
   * instantiation, and the key free functions are registered under. */
  virtual string buildFullName() const override
  {
    string s = name;
    if (templateParams.size() > 0) {
      s += "<";
      for (int i = 0; i < templateParams.size(); i++) {
        if (i > 0) {
          s += ",";
        }
        s += templateParams[i].type->buildFullName();
      }
      s += ">";
    }
    s += "[";
    for (int i = 0; i < params.size(); i++) {
      if (i > 0) {
        s += ",";
      }
      s += params[i].type->buildFullName();
    }
    s += "]";
    return s;
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

} // namespace litestl::binding::types

namespace litestl::binding {

template <typename T> struct TypeTag {
  using type = T;
};

namespace detail {

/* Delays a lookup of Binder<X> until a template using it is instantiated, so
 * the explicit specializations in binding.h only need to be visible by then. */
template <typename X, typename> struct Dependent {
  using type = X;
};

/* `const T&` to these binds as a plain T: the thunk ABI is the same (args[i]
 * points at a T either way) and the runtimes build them from JS values. */
template <typename T> struct IsValueLikeParam : std::false_type {};
template <> struct IsValueLikeParam<util::string> : std::true_type {};
template <typename E> struct IsValueLikeParam<std::span<E>> : std::true_type {};

/** Descriptor for a parameter of C++ type T. Pointers default to non-null; use
 * Method::argIsNullable to relax that. */
template <typename T> const BindingBase *bindParam()
{
  using U = std::remove_cvref_t<T>;
  if constexpr (std::is_same_v<U, const char *> || std::is_same_v<U, char *>) {
    return Bind<typename Dependent<const char *, T>::type>();
  } else if constexpr (std::is_pointer_v<U>) {
    using P = std::remove_cv_t<std::remove_pointer_t<U>>;
    if constexpr (std::is_void_v<P>) {
      return Bind<typename Dependent<void *, T>::type>();
    } else {
      auto *p = new types::Pointer(Bind<P>());
      p->isNonNull = true;
      return p;
    }
  } else if constexpr (std::is_lvalue_reference_v<T> &&
                       !(std::is_const_v<std::remove_reference_t<T>> &&
                         IsValueLikeParam<U>::value))
  {
    return new types::Reference(Bind<U>());
  } else {
    return Bind<U>();
  }
}

/** Descriptor for a return type R, or nullptr for void. */
template <typename R> const BindingBase *bindReturn()
{
  using U = std::remove_cvref_t<R>;
  if constexpr (std::is_void_v<R>) {
    return nullptr;
  } else if constexpr (std::is_same_v<U, const char *> || std::is_same_v<U, char *>) {
    return Bind<typename Dependent<const char *, R>::type>();
  } else if constexpr (std::is_pointer_v<U>) {
    using P = std::remove_cv_t<std::remove_pointer_t<U>>;
    if constexpr (std::is_void_v<P>) {
      return Bind<typename Dependent<void *, R>::type>();
    } else {
      return new types::Pointer(Bind<P>());
    }
  } else if constexpr (std::is_reference_v<R>) {
    return new types::Reference(Bind<U>());
  } else {
    return Bind<U>();
  }
}

/** Reads argument I of the thunk ABI: args[I] points at the value for value
 * and reference parameters alike, and at a pointer-sized slot for pointers. */
template <typename A> decltype(auto) thunkArg(void *p)
{
  using U = std::remove_cvref_t<A>;
  if constexpr (std::is_rvalue_reference_v<A>) {
    return std::move(*static_cast<U *>(p));
  } else {
    return *static_cast<U *>(p);
  }
}

/** Stores a call's result in `ret`: values are placement-constructed there,
 * references store the referent's address (matching how the runtimes read a
 * Reference binding). */
template <typename R, typename Call> void storeReturn(void *ret, Call &&call)
{
  if constexpr (std::is_void_v<R>) {
    call();
  } else if constexpr (std::is_reference_v<R>) {
    auto &r = call();
    *static_cast<void **>(ret) =
        const_cast<void *>(static_cast<const void *>(std::addressof(r)));
  } else {
    using U = std::remove_cv_t<R>;
    new (ret) U(call());
  }
}

template <typename Tuple, size_t... I>
void fillParamsImpl(util::Vector<types::MethodParam> &out, std::index_sequence<I...>)
{
  (out.append(types::MethodParam{string(std::format("arg{}", I + 1).c_str()),
                                 bindParam<std::tuple_element_t<I, Tuple>>()}),
   ...);
}

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

template <class T> struct FunctionTraits;

template <class R, class... A> struct FunctionTraits<R (*)(A...)> {
  using return_type = R;
  using args_tuple = std::tuple<A...>;
  static constexpr size_t arity = sizeof...(A);
};
template <class R, class... A> struct FunctionTraits<R (*)(A...) noexcept> {
  using return_type = R;
  using args_tuple = std::tuple<A...>;
  static constexpr size_t arity = sizeof...(A);
};

} // namespace detail

namespace types {

template <auto Mfp> struct MethodBuilder {
  using Traits = detail::MethodTraits<decltype(Mfp)>;
  using class_type = typename Traits::class_type;
  using return_type = typename Traits::return_type;
  static constexpr bool is_const = Traits::is_const;
  static constexpr size_t arity = Traits::arity;

  static const BindingBase *returnType()
  {
    return detail::bindReturn<return_type>();
  }

  static void fillParams(Vector<MethodParam> &out)
  {
    detail::fillParamsImpl<typename Traits::args_tuple>(out,
                                                        std::make_index_sequence<arity>{});
  }

  static void thunk(void *self, void **args, void *ret)
  {
    invokeImpl(
        static_cast<class_type *>(self), args, ret, std::make_index_sequence<arity>{});
  }

private:
  template <size_t I> using arg_t = std::tuple_element_t<I, typename Traits::args_tuple>;

  template <size_t... I>
  static void
  invokeImpl(class_type *self, void **args, void *ret, std::index_sequence<I...>)
  {
    (void)args;
    detail::storeReturn<return_type>(
        ret, [&]() -> decltype(auto) {
          return (self->*Mfp)(detail::thunkArg<arg_t<I>>(args[I])...);
        });
  }
};

/** Builds descriptors for free functions and static member functions. The
 * thunk ignores `self`. */
template <auto Fn> struct FunctionBuilder {
  using Traits = detail::FunctionTraits<decltype(Fn)>;
  using return_type = typename Traits::return_type;
  static constexpr size_t arity = Traits::arity;

  static const BindingBase *returnType()
  {
    return detail::bindReturn<return_type>();
  }

  static void fillParams(Vector<MethodParam> &out)
  {
    detail::fillParamsImpl<typename Traits::args_tuple>(out,
                                                        std::make_index_sequence<arity>{});
  }

  static void thunk(void *, void **args, void *ret)
  {
    invokeImpl(args, ret, std::make_index_sequence<arity>{});
  }

private:
  template <size_t I> using arg_t = std::tuple_element_t<I, typename Traits::args_tuple>;

  template <size_t... I>
  static void invokeImpl(void **args, void *ret, std::index_sequence<I...>)
  {
    (void)args;
    detail::storeReturn<return_type>(ret, [&]() -> decltype(auto) {
      return Fn(detail::thunkArg<arg_t<I>>(args[I])...);
    });
  }
};

/** A Method descriptor for the free (or static member) function `Fn`. */
template <auto Fn> Method *makeFunction(string name)
{
  using Builder = FunctionBuilder<Fn>;
  Method *m = new Method(name);
  m->returnType = Builder::returnType();
  Builder::fillParams(m->params);
  m->thunk = &Builder::thunk;
  m->isStatic = true;
  return m;
}

/** A Method descriptor for the member function `Mfp`. */
template <auto Mfp> Method *makeMethod(string name)
{
  using Builder = MethodBuilder<Mfp>;
  Method *m = new Method(name);
  m->returnType = Builder::returnType();
  Builder::fillParams(m->params);
  m->thunk = &Builder::thunk;
  m->isConst = Builder::is_const;
  return m;
}

} // namespace types

namespace detail {
/** Applies template-parameter names to one instantiation per type in Ts. */
template <typename... Ts, typename Make, typename Add>
void bindTemplateInstances(util::Vector<const char *, 8> tnames, Make make, Add add)
{
  if (tnames.size() != 1) {
    printf("bindTemplateInstances: one template parameter name expected, got %d\n",
           int(tnames.size()));
    abort();
  }
  (add(&make(TypeTag<Ts>{})->addTemplateParam(Bind<Ts>(), tnames[0])), ...);
}
} // namespace detail

} // namespace litestl::binding

#define MARGS(...) litestl::util::Vector<const char *, 8>({__VA_ARGS__})
/** Template parameter names, e.g. TNAMES("T"). */
#define TNAMES(...) litestl::util::Vector<const char *, 8>({__VA_ARGS__})
/**
 * To set argument names, do e.g. BIND_STRUCT_METHOD(st, foo, MARGS("arg1", "arg2"));
 */
#define BIND_STRUCT_METHOD(def, mname, ARGNAMES)                                         \
  def->template _addMethod<&std::remove_reference_t<decltype(*(def)->type_null)>::mname>( \
         #mname)                                                                         \
      ->setArgNames(ARGNAMES)

/** Note: see Struct.setMethodArgNames */
#define BIND_STRUCT_METHOD_SIG(def, mname, RET, ARGNAMES, ARGS)                          \
  do {                                                                                   \
    using _BSM_C = std::remove_reference_t<decltype(*(def)->type_null)>;                 \
    constexpr auto _BSM_FP = static_cast<RET(_BSM_C::*) ARGS>(&_BSM_C::mname);           \
    auto *_BSM_m = ::litestl::binding::types::makeMethod<_BSM_FP>(#mname);               \
    (def)->addMethod(_BSM_m);                                                            \
    _BSM_m->setArgNames(ARGNAMES);                                                       \
  } while (0)

/** A static member function, exposed on the JS side as `Struct.mname(...)`. */
#define BIND_STRUCT_STATIC_METHOD(def, mname, ARGNAMES)                                  \
  def->template _addStaticMethod<                                                       \
      &std::remove_reference_t<decltype(*(def)->type_null)>::mname>(#mname)              \
      ->setArgNames(ARGNAMES)

/**
 * A member function template, one instantiation per trailing type:
 *   BIND_STRUCT_METHOD_TEMPLATE(st, scale, TNAMES("T"), MARGS("s"), float, double);
 */
#define BIND_STRUCT_METHOD_TEMPLATE(def, mname, TNAMES_, ARGNAMES, ...)                  \
  do {                                                                                   \
    using _BSMT_C = std::remove_reference_t<decltype(*(def)->type_null)>;                \
    auto _BSMT_def = (def);                                                              \
    ::litestl::binding::detail::bindTemplateInstances<__VA_ARGS__>(                      \
        TNAMES_,                                                                         \
        []<typename _BSMT_T>(::litestl::binding::TypeTag<_BSMT_T>) {                     \
          auto *m = ::litestl::binding::types::makeMethod<                               \
              &_BSMT_C::template mname<_BSMT_T>>(#mname);                                \
          m->setArgNames(ARGNAMES);                                                      \
          return m;                                                                      \
        },                                                                               \
        [_BSMT_def](::litestl::binding::types::Method *m) {                              \
          _BSMT_def->addMethod(m);                                                       \
        });                                                                              \
  } while (0)

/**
 * A free function, registered with the BindingManager under its qualified
 * name. Returns the descriptor for chaining (.returnsOwned(), .setJsName()).
 *   BIND_FUNCTION(mgr, "ns::createCube", ns::createCube, MARGS("dimen", "size"));
 */
#define BIND_FUNCTION(mgr, qualifiedName, fn, ARGNAMES)                                  \
  (mgr)                                                                                  \
      ->addFunction(::litestl::binding::types::makeFunction<&fn>(qualifiedName))         \
      ->setArgNames(ARGNAMES)

/** BIND_FUNCTION for an overloaded function: names the signature to bind.
 *   BIND_FUNCTION_SIG(mgr, "ns::area", ns::area, float, (const Box &), MARGS("b"));
 */
#define BIND_FUNCTION_SIG(mgr, qualifiedName, fn, RET, ARGS, ARGNAMES)                   \
  (mgr)                                                                                  \
      ->addFunction(                                                                     \
          ::litestl::binding::types::makeFunction<static_cast<RET(*) ARGS>(&fn)>(        \
              qualifiedName))                                                            \
      ->setArgNames(ARGNAMES)

/** One explicit instantiation of a function template; use for templates with
 * several parameters:
 *   BIND_FUNCTION_INSTANCE(mgr, "ns::f", (ns::f<float, int>), MARGS("a"),
 *                          TARG(float, "T"), TARG(int, "U"));
 */
#define TARG(type, name)                                                                 \
  +[](::litestl::binding::types::Method *m) {                                            \
    m->addTemplateParam(::litestl::binding::Bind<type>(), name);                         \
  }
#define BIND_FUNCTION_INSTANCE(mgr, qualifiedName, fn, ARGNAMES, ...)                    \
  [&]() -> ::litestl::binding::types::Method & {                                         \
    auto *_BFI_m = ::litestl::binding::types::makeFunction<&fn>(qualifiedName);          \
    _BFI_m->setArgNames(ARGNAMES);                                                       \
    for (auto _BFI_f : {__VA_ARGS__}) {                                                  \
      _BFI_f(_BFI_m);                                                                    \
    }                                                                                    \
    return *(mgr)->addFunction(_BFI_m);                                                  \
  }()

/**
 * A single-parameter function template, one instantiation per trailing type:
 *   BIND_FUNCTION_TEMPLATE(mgr, "ns::addCube", ns::addCube, TNAMES("T"),
 *                          MARGS("dimen"), float, double);
 */
#define BIND_FUNCTION_TEMPLATE(mgr, qualifiedName, fn, TNAMES_, ARGNAMES, ...)           \
  do {                                                                                   \
    auto _BFT_mgr = (mgr);                                                               \
    ::litestl::binding::detail::bindTemplateInstances<__VA_ARGS__>(                      \
        TNAMES_,                                                                         \
        []<typename _BFT_T>(::litestl::binding::TypeTag<_BFT_T>) {                       \
          auto *m = ::litestl::binding::types::makeFunction<&fn<_BFT_T>>(qualifiedName); \
          m->setArgNames(ARGNAMES);                                                      \
          return m;                                                                      \
        },                                                                               \
        [_BFT_mgr](::litestl::binding::types::Method *m) { _BFT_mgr->addFunction(m); });  \
  } while (0)
