#pragma once

#include "litestl/binding/binding.h"
#include "litestl/util/map.h"
#include "litestl/util/string.h"
#include "litestl/util/vector.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>

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

  /** Registers a type. Types are deduplicated by full name, so adding one twice
   * is harmless. Methods passed here are treated as free functions. */
  void add(const BindingBase *binding)
  {
    if (binding->type == BindingType::Method) {
      addFunction(const_cast<types::Method *>(static_cast<const types::Method *>(binding)));
      return;
    }

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
        addSignatureTypes(m);
      }
      for (const types::Constructor *c : st->constructors) {
        for (const auto &p : c->params) {
          add(p.type);
        }
      }
      for (const auto &tp : st->templateParams) {
        if (tp.type->type != BindingType::Literal) {
          add(tp.type);
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

  /**
   * Registers a free function under its full name
   * (`ns::name<template args>[param types]`). Registering the same full name
   * twice is a hard error. Returns `m` for chaining.
   */
  types::Method *addFunction(types::Method *m)
  {
    string key = m->buildFullName();
    if (bindings.contains(key)) {
      printf("%s\n",
             std::format("binding: function {} registered twice", key).c_str());
      abort();
    }
    bindings.add(key, m);
    functions.append(m);
    addSignatureTypes(m);
    return m;
  }

  /**
   * Checks that functions sharing a JS name are instantiations of one template;
   * plain overloads need distinct names (Method::setJsName). Runs lazily
   * because setJsName is usually chained after registration.
   */
  bool checkFunctions(string *error = nullptr) const
  {
    for (const auto &group : functionGroups()) {
      const types::Method *first = group[0];
      for (const types::Method *m : group) {
        if (group.size() > 1 && (m->templateParams.size() == 0 || m->name != first->name))
        {
          if (error) {
            *error = string(std::format("binding: functions {} and {} share the JS name {}; "
                                        "rename one with setJsName()",
                                        first->buildFullName(),
                                        m == first ? group[1]->buildFullName()
                                                   : m->buildFullName(),
                                        first->qualifiedJsName())
                                .c_str());
          }
          return false;
        }
      }
    }
    return true;
  }

  /** checkFunctions(), aborting with the message on failure. */
  void validate() const
  {
    string error;
    if (!checkFunctions(&error)) {
      printf("%s\n", error.c_str());
      abort();
    }
  }

  /** Free functions grouped by qualified JS name, sorted by that name. Each
   * group is one function or the instantiations of one template. */
  util::Vector<util::Vector<const types::Method *>> functionGroups() const
  {
    util::Map<string, util::Vector<const types::Method *>> byName;
    util::Vector<string> names;
    for (const types::Method *m : functions) {
      string key = m->qualifiedJsName();
      auto *group = byName.lookup_ptr(key);
      if (!group) {
        byName.add(key, util::Vector<const types::Method *>());
        names.append(key);
        group = byName.lookup_ptr(key);
      }
      group->append(m);
    }
    std::sort(names.begin(), names.end(), [](const string &a, const string &b) {
      return strcmp(a.c_str(), b.c_str()) < 0;
    });
    util::Vector<util::Vector<const types::Method *>> result;
    for (const string &name : names) {
      result.append(byName.lookup(name));
    }
    return result;
  }

  const util::Vector<const types::Method *> &getFunctions() const
  {
    return functions;
  }

  const binding_map &getBindings() const
  {
    return bindings;
  }

private:
  void addSignatureTypes(const types::Method *m)
  {
    if (m->returnType) {
      add(m->returnType);
    }
    for (const auto &p : m->params) {
      add(p.type);
    }
    for (const auto &tp : m->templateParams) {
      if (tp.type->type != BindingType::Literal) {
        add(tp.type);
      }
    }
  }

  binding_map bindings;
  util::Vector<const types::Method *> functions;
};
} // namespace litestl::binding
