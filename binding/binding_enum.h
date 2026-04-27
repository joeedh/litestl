#pragma once
#include "binding/binding_base.h"
#include "binding_number.h"

namespace litestl::binding::types {
struct EnumItem {
  string name;
  int value;
};
struct Enum : public BindingBase {
  util::Vector<EnumItem> items;
  int baseSize;
  bool isBitMask = false;

  Enum(string name, size_t size) : BindingBase(BindingType::Enum, name), baseSize(size)
  {
  }
  Enum(const Enum &b) = default;

  template<typename T>
  void addItem(const string name, T value)
  {
    items.append({name, int(value)});
  }

  size_t getSize() const override
  {
    return baseSize;
  }

  string buildFullName() const override
  {
    return name;
  }

  BindingBase *clone() override
  {
    return new Enum(*this);
  }
};
} // namespace litestl::binding::types
