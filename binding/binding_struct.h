#pragma once

#include "binding_base.h"
#include "binding_bind.h"
#include "binding_method.h"
#include "binding_types.h"
#include "util/vector.h"
#include <concepts>
#include <cstring>
#include <functional>
#include <type_traits>

/** Descriptors carry the fields coredcc's interfaces need: Struct::isInterface and
 * typeKey, Method::isOptional, Pointer/Reference::isConst, adaptor methods. */
#define LITESTL_HAS_IFACE_DESCRIPTORS 1

namespace litestl::binding::types {
using util::Vector;

struct Constructor;

struct StructMember {
  string name;
  size_t offset;
  const BindingBase *type;
};

struct _StructBase : public BindingBase {
  Vector<StructMember> members;
  Vector<Method *> methods;
  Vector<Constructor *> constructors;
  Vector<StructTemplate> templateParams;

  size_t structSize;
  /** An interface: its methods are slots that other structs implement (coredcc). */
  bool isInterface = false;
  /** Unique per C++ type (set by Struct<T>), so two types bound under one name can be
   * told apart. Null for descriptors not built by Struct<T>. */
  const void *typeKey = nullptr;
  // is set by Struct child template class below
  std::function<void(void *)> *destructorThunk = nullptr;

  _StructBase(string name, size_t size)
      : BindingBase(BindingType::Struct, name), structSize(size)
  {
    //
  }
  _StructBase(const _StructBase &b)
      : BindingBase(b.type, b.name), members(b.members), methods(b.methods),
        constructors(b.constructors), templateParams(b.templateParams),
        structSize(b.structSize), isInterface(b.isInterface), typeKey(b.typeKey)
  {
  }
  virtual ~_StructBase()
  {
    if (destructorThunk) {
      delete destructorThunk;
    }
  }
  virtual size_t getSize() const override
  {
    return structSize;
  }
  virtual string buildFullName() const override
  {
    string s = name;

    if (templateParams.size() > 0) {
      s += "<";
      for (size_t i = 0; i < templateParams.size(); i++) {
        if (i > 0) {
          s += ",";
        }
        s += templateParams[i].type->buildFullName();
      }
      s += ">";
    }
    return s;
  }

  /**
   * Look up a constructor registered on this struct by name (e.g. "main",
   * "io"). Returns nullptr if no match. Defined out-of-line in
   * binding_constructor.h since Constructor is only forward-declared here.
   */
  const Constructor *findConstructor(const string &name) const;

  /** The first method named `methodName`, or nullptr. */
  const Method *findMethod(const char *methodName) const
  {
    for (const Method *m : methods) {
      if (strcmp(m->name.c_str(), methodName) == 0) {
        return m;
      }
    }
    return nullptr;
  }
};

/* Not const: identical read-only statics may be folded together by the linker (/OPT:ICF). */
template <typename T> struct TypeKeyHolder {
  static inline char key = 0;
};

/** The key Struct<T> stores in _StructBase::typeKey. */
template <typename T> const void *typeKeyOf()
{
  return &TypeKeyHolder<std::remove_cv_t<T>>::key;
}

template <typename T> struct DestructorThunk {
  /** Calls destructor, but does not release the underlying memory */
  std::function<void(void *)> destructor;

  DestructorThunk()
      : destructor([](void *obj) {
          T *typedObj = static_cast<T *>(obj);
          typedObj->~T();
        })
  {
  }

  DestructorThunk(const DestructorThunk &other) = delete;
  DestructorThunk(DestructorThunk &&other) = delete;
};

template <typename CLS> struct Struct : public _StructBase {
  using struct_type = CLS;

  struct_type *type_null;

  Struct(string name, size_t size) : _StructBase(name, size)
  {
    typeKey = typeKeyOf<CLS>();
    destructorThunk = new std::function<void(void *)>([](void *obj) {
      CLS *typedObj = static_cast<CLS *>(obj);
      typedObj->~CLS();
    });
  }
  Struct(const Struct &b) : _StructBase(b)
  {
    destructorThunk = b.destructorThunk;
  }

  Struct &setNonNull(string memberName)
  {
    for (auto &member : members) {
      if (member.name == memberName) {
        Pointer *ptr = static_cast<Pointer *>(member.type->clone());
        ptr->isNonNull = true;
        member.type = ptr;
        return *this;
      }
    }

    printf("setNonNull: unknown member: %s\n", memberName.c_str());
    abort();
  }

  void inherit(_StructBase *parent)
  {
    for (auto &member : parent->members) {
      if (!members.contains(
              [&member](const StructMember &m) { return m.name == member.name; }))
        ;
      members.append(member);
    }
    for (auto &method : parent->methods) {
      if (!methods.contains(
              [&method](const Method *m) { return m->name == method->name; }))
        ;
      methods.append(method);
    }
  }

  virtual BindingBase *clone() const override
  {
    return static_cast<BindingBase *>(new Struct(*this));
  }
  const BindingBase *add(string name, size_t offset, const BindingBase *type)
  {
    members.append({name, offset, type});
    return type;
  }
  void addMethod(Method *m)
  {
    methods.append(m);
  }

  // used by BIND_STRUCT_METHOD macro
  template <auto Mfg> Method *_addMethod(const char *mname)
  {
    Method *method = makeMethod<Mfg>(mname);
    addMethod(method);
    return method;
  }
  // used by BIND_ADAPTOR_METHOD macro
  template <auto Fn> Method *_addAdaptorMethod(const char *mname)
  {
    static_assert(std::is_same_v<typename detail::AdaptorTraits<decltype(Fn)>::class_type,
                                 CLS>,
                  "BIND_ADAPTOR_METHOD: the function's first parameter must be CLS&");
    Method *method = makeAdaptorMethod<Fn>(mname);
    addMethod(method);
    return method;
  }
  // used by BIND_STRUCT_STATIC_METHOD macro
  template <auto Fn> Method *_addStaticMethod(const char *mname)
  {
    Method *method = makeFunction<Fn>(mname);
    addMethod(method);
    return method;
  }
  void addConstructor(Constructor *c)
  {
    constructors.append(c);
  }
  void addTemplateParam(const BindingBase *type, string name)
  {
    templateParams.append({name, type});
  }
};

} // namespace litestl::binding::types
namespace litestl::binding {

template <typename CLS>
concept ClassBindingReq = requires(types::Struct<CLS> *def) {
  { CLS::defineBindings() } -> std::convertible_to<const types::Struct<CLS> *>;
  std::is_reference_v<CLS> == false;
};

template <ClassBindingReq CLS> struct Binder<CLS> {
  static const BindingBase *bind()
  {
    return CLS::defineBindings();
  }
};
} // namespace litestl::binding

#define BIND_STRUCT_MEMBER(def, field)                                                   \
  def->add(#field,                                                                       \
           offsetof(std::remove_reference_t<decltype(*def->type_null)>, field),          \
           binding::Bind<                                                                \
               decltype(std::remove_reference_t<decltype(*def->type_null)>::field)>())
