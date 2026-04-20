#include "litestl/binding/binding.h"
#include "litestl/util/map.h"
#include "litestl/util/string.h"
#include "litestl/util/vector.h"

namespace litestl::binding {
struct BindingManager {
  litestl::util::Map<litestl::util::string, const BindingBase *> bindings;
  void add(const BindingBase *binding) {
    if (bindings.contains(binding->name)) {
      return;
    }
    
    bindings.add(binding->name, binding);

    if (binding->type == BindingType::Struct) {
      const types::_StructBase *st = static_cast<const types::_StructBase *>(binding);
      for (const auto &member : st->members) {
        add(member.type);
      }
      for (const types::Method *m : st->methods) {
        if (m->returnType) {
          add(m->returnType);
        }
        for (const auto &p : m->params) {
          add(p.type);
        }
      }
    }
  }
};
} // namespace litestl::binding

