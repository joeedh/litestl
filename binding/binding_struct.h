#pragma once

#include "binding_base.h"
#include "binding_method.h"
#include "binding_types.h"
#include "util/vector.h"
#include <functional>
#include <type_traits>

namespace litestl::binding::types {
using util::Vector;

struct Constructor;

struct StructTemplate {
  string name;
  const BindingBase *type;
};

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
  // is set by Struct child template class below
  std::function<void(void *)> *destructorThunk = nullptr;

  _StructBase(string name, size_t size)
      : BindingBase(BindingType::Struct, name), structSize(size)
  {
    //
  }
  _StructBase(const _StructBase &b)
      : BindingBase(b.type, b.name), members(b.members), methods(b.methods),
        constructors(b.constructors), structSize(b.structSize)
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
};

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
    destructorThunk = new std::function<void(void *)>([](void *obj) {
      CLS *typedObj = static_cast<CLS *>(obj);
      typedObj->~CLS();
    });
  }
  Struct(const Struct &b) : _StructBase(b.name, b.structSize)
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
    auto *method = new ::litestl::binding::types::Method(mname);
    using Builder = MethodBuilder<Mfg>;

    method->returnType = Builder::returnType();
    Builder::fillParams(method->params);
    method->thunk = &Builder::thunk;
    method->isConst = Builder::is_const;

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

template <typename CLS>
const BindingBase *Bind()
  requires ClassBindingReq<CLS>
{
  return CLS::defineBindings();
}
} // namespace litestl::binding

#define BIND_STRUCT_MEMBER(def, field)                                                   \
  def->add(#field,                                                                       \
           offsetof(std::remove_reference_t<decltype(*def->type_null)>, field),          \
           binding::Bind<                                                                \
               decltype(std::remove_reference_t<decltype(*def->type_null)>::field)>())
