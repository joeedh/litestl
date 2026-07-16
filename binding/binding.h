#pragma once

#include "binding_base.h"
#include "binding_bind.h"
#include "binding_constructor.h"
#include "binding_constructor_builder.h"
#include "binding_enum.h"
#include "binding_literal.h"
#include "binding_method.h"
#include "binding_number.h"
#include "binding_struct.h"
#include "binding_template.h"
#include "binding_types.h"
#include "binding_utils.h"

#include "../math/math_bindings.h"

#include "generators/python.h"
#include "generators/typescript.h"

// XXX vector binding utility, find where to put it other then this file!
namespace litestl::binding {
using litestl::util::Vector;

template <> struct Binder<void *> {
  static const BindingBase *bind()
  {
    return new types::Number<unsigned char>(sizeof(void *) == 4 ? NumberType::Int32
                                                                : NumberType::Int64,
                                            "pointer",
                                            NumberFlags::Unsigned);
  }
};

template <typename T>
concept IsVector = requires(T v) {
  // { Bind<T::value_type>() } -> std::derived_from<const BindingBase *>;
  std::same_as<typename T::is_litestl_vector, std::true_type>;
};

/** vector binding */
template <IsVector VEC> struct Binder<VEC> {
  static const BindingBase *bind()
  {
    types::Struct<VEC> *st =
        new types::Struct<VEC>("litestl::util::Vector", sizeof(VEC));
    const BindingBase *bindT = Bind<typename VEC::value_type>();

    if (bindT->type == BindingType::Pointer) {
      // do not allow null for vector elements
      types::Pointer *p = static_cast<types::Pointer *>(
          static_cast<const types::Pointer *>(bindT)->clone());
      p->isNonNull = true;
      bindT = p;
    }

    BIND_STRUCT_DEFAULT_CONSTRUCTOR(st);
    BIND_STRUCT_CONSTRUCTOR(st, "ptrWithCount", typename VEC::value_type *, int);

    st->constructors[1]->params[0].type =
        new types::ParentTemplateParam("T", 0, st->constructors[1]->params[0].type);

    // resize is a templated method, build manually
    types::Method *m = new types::Method("resize");
    m->params.append({"newsize", Bind<int>()});
    m->thunk = [](void *self, void **args, void *ret) {
      VEC *v = static_cast<VEC *>(self);
      int *newsize = static_cast<int *>(args[0]);
      v->resize(*newsize);
    };
    // XXX need to create a void type?
    // allow returnType to be null?
    m->returnType = new types::Number<int>(NumberType::Int32, "void", NumberFlags::None);
    st->methods.append(m);

    // resize but don't invoke constructors or destructors
    m = new types::Method("resize_no_construct_destruct");
    m->params.append({"newsize", Bind<int>()});
    m->thunk = [](void *self, void **args, void *ret) {
      VEC *v = static_cast<VEC *>(self);
      int *newsize = static_cast<int *>(args[0]);
      v->template resize<false>(*newsize);
    };

    // XXX need to create a void type?
    // allow returnType to be null?
    m->returnType = new types::Number<int>(NumberType::Int32, "void", NumberFlags::None);
    st->methods.append(m);

    st->addTemplateParam(bindT, "T");
    st->addTemplateParam(
        new types::NumLitType(VEC::staticSize, "static_size", Bind<int>()),
        "static_size");

    return st;
  }
};

// string binding
template <> struct Binder<litestl::util::string> {
  static const BindingBase *bind()
  {
    return new types::Struct<litestl::util::string>("litestl::util::String",
                                                    sizeof(litestl::util::string));
  }
};
} // namespace litestl::binding

#include "binding_union.h"
