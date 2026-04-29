#pragma once

#include "litestl/binding/binding.h"
#include "litestl/util/map.h"
#include "litestl/util/string.h"
#include "litestl/util/vector.h"

namespace litestl::binding {
struct BindingManager {
  using binding_map = litestl::util::Map<litestl::util::string, const BindingBase *>;

  void add(const BindingBase *binding)
  {
    if (bindings.contains(binding->name)) {
      return;
    }

    bindings.add(binding->buildFullName(), binding);

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
      for (const types::Constructor *c : st->constructors) {
        for (const auto &p : c->params) {
          add(p.type);
        }
      }
    }
  }

  const binding_map &getBindings() const { return bindings; }
private:
  binding_map bindings;
};
} // namespace litestl::binding
