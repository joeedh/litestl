#pragma once

#include "litestl/binding/binding.h"
#include "litestl/util/map.h"
#include "litestl/util/string.h"
#include "litestl/util/vector.h"

namespace litestl::binding {
struct BindingManager {
  using binding_map = litestl::util::Map<litestl::util::string, const BindingBase *>;

  BindingManager()
  {
    // add a bunch of numeric types
    add(Bind<bool>());
    add(Bind<float>());
    add(Bind<double>());
    // not sure why signed char, char and unisgned char seem to be
    // distinct types in the type system, could be clang bug
    // TODO: check if this is still the case
    add(Bind<signed char>());
    add(Bind<unsigned char>());
    add(Bind<char>());
    add(Bind<short>());
    add(Bind<unsigned short>());
    add(Bind<int>());
    add(Bind<unsigned int>());
    add(Bind<int64_t>());
    add(Bind<uint64_t>());

    // add a bunch of vectors
    add(Bind<util::Vector<bool>>());
    add(Bind<util::Vector<float>>());
    add(Bind<util::Vector<double>>());
    add(Bind<util::Vector<char>>());
    add(Bind<util::Vector<signed char>>());
    add(Bind<util::Vector<char>>());
    add(Bind<util::Vector<short>>());
    add(Bind<util::Vector<unsigned short>>());
    add(Bind<util::Vector<int>>());
    add(Bind<util::Vector<unsigned int>>());
    add(Bind<util::Vector<uint64_t>>());
    add(Bind<util::Vector<int64_t>>());
    add(Bind<util::Vector<void *>>());
    add(Bind<void *>());
  }

  void add(const BindingBase *binding)
  {
    auto fullName = binding->buildFullName();

    if (bindings.contains(fullName)) {
      return;
    }
    bindings.add(fullName, binding);

    switch (binding->type) {
    case BindingType::Struct: {
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
      break;
    }
    case BindingType::Array:
      bindings.add(binding->name, binding);
      add(static_cast<const types::Array<void> *>(binding)->arrayType);
      break;
    case BindingType::Pointer:
      bindings.add(binding->name, binding);
      add(static_cast<const types::Pointer *>(binding)->ptrType);
      break;
    case BindingType::Reference:
      bindings.add(binding->name, binding);
      add(static_cast<const types::Reference *>(binding)->refType);
      break;
    case BindingType::Enum:
      bindings.add(binding->name, binding);
      break;
    case BindingType::Union: {
      const types::Union *u = static_cast<const types::Union *>(binding);
      bindings.add(binding->name, binding);
      add(u->disPropType);
      for (auto &pair : u->structs) {
        add(pair.type);
      }
      break;
    }
    case BindingType::Method:
    case BindingType::Boolean:
    case BindingType::Constructor:
    case BindingType::Number:
    case BindingType::ParentTemplParam:
    case BindingType::Literal:
      // do nothing
      break;
    }
  }

  const binding_map &getBindings() const
  {
    return bindings;
  }

private:
  binding_map bindings;
};
} // namespace litestl::binding
