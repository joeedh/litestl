#pragma once

#include "binding_base.h"
#include "binding_number.h"
#include "binding_utils.h"
#include "util/vector.h"
#include "binding_method_builder.h"

#include <format>
#include <tuple>
#include <type_traits>
#include <utility>

namespace litestl::binding::types {
using util::string;
using util::Vector;

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
