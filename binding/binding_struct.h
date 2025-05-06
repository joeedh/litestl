#include "binding_base.h"
#include "binding_types.h"
#include "util/vector.h"

namespace litestl::binding::types {
using util::Vector;

struct StructMember {
  string name;
  size_t offset;
  const BindingBase *type;
};

template <typename CLS = int> struct Struct : public BindingBase {
  using struct_type = CLS;

  struct_type *type_null;

  Vector<StructMember> members;
  size_t structSize;

  Struct(string name, size_t size)
      : BindingBase(BindingType::Struct, name), structSize(size)
  {
    //
  }
  Struct(const Struct &b)
      : BindingBase(b.type, b.name), structSize(b.structSize), members(b.members)
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
  virtual size_t getSize() override
  {
    return structSize;
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
