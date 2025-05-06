#pragma once

#include "binding.h"
#include "util/alloc.h"

namespace litestl::binding {
using util::string;

namespace types {

struct Boolean : public BindingBase {
  Boolean() : BindingBase(BindingType::Boolean, "boolean")
  {
    //
  }
  virtual BindingBase *clone()
  {
    return static_cast<BindingBase *>(new Boolean(*this));
  }
  virtual size_t getSize()
  {
    return sizeof(bool);
  }
};

template<typename T>
struct Number : public BindingBase {
  using value_type = T;
  NumberType subtype;
  NumberFlags flags;


  Number(NumberType subtype, NumberFlags flags=NumberFlags::None)
      : BindingBase(BindingType::Boolean, "number"), subtype(subtype), flags(flags)
  {
    //
  }
  Number(const Number &b) : BindingBase(b), subtype(b.subtype), flags(b.flags)
  {
    //
  }

  virtual BindingBase *clone()
  {
    return static_cast<BindingBase *>(new Number(*this));
  }
  virtual size_t getSize()
  {
    switch (subtype) {
    case NumberType::Int8:
      return 1;
    case NumberType::Int16:
      return 2;
    case NumberType::Int32:
      return 4;
    case NumberType::Int64:
      return 8;
    case NumberType::Float32:
      return 4;
    case NumberType::Float64:
      return 8;
    }
    return 0;
  }
};

template <typename T> struct Array : public BindingBase {
  T *arrayType;
  size_t arraySize;

  Array(T *arrayType, size_t arraySize)
      : BindingBase(BindingType::Array, "array"), arrayType(arrayType),
        arraySize(arraySize)
  {
  }
  Array(const Array &b) : BindingBase(b), arrayType(b.arrayType), arraySize(b.arraySize)
  {
  }
  virtual BindingBase *clone()
  {
    return static_cast<BindingBase *>(new Number(*this));
  }
  virtual size_t getSize()
  {
    return arrayType->getSize() * arraySize;
  }
};

} // namespace types
} // namespace litestl::binding
