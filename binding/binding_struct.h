#pragma once

#include "binding_base.h"
#include "binding_types.h"
#include "util/vector.h"

namespace litestl::binding::types {
using util::Vector;

struct Method;

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
  Vector<const Method *> methods;
  Vector<StructTemplate> templateParams;

  size_t structSize;

  _StructBase(string name, size_t size)
      : BindingBase(BindingType::Struct, name), structSize(size)
  {
    //
  }
  _StructBase(const _StructBase &b)
      : BindingBase(b.type, b.name), members(b.members), methods(b.methods),
        structSize(b.structSize)
  {
  }
  virtual size_t getSize() override
  {
    return structSize;
  }
};

template <typename CLS = int> struct Struct : public _StructBase {
  using struct_type = CLS;

  struct_type *type_null;

  Struct(string name, size_t size) : _StructBase(name, size)
  {
    //
  }
  Struct(const Struct &b) : _StructBase(b.name, b.structSize)
  {
  }

  virtual BindingBase *clone() override
  {
    return static_cast<BindingBase *>(new Struct(*this));
  }
  void add(string name, size_t offset, const BindingBase *type)
  {
    members.append({name, offset, type});
  }
  void addMethod(const Method *m)
  {
    methods.append(m);
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
  {
    CLS::defineBindings()
  } -> std::convertible_to<const types::Struct<CLS> *>;
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
