#pragma once

#include "binding_base.h"
#include "util/alloc.h"

namespace litestl::binding {
using util::string;

namespace types {

struct Boolean : public BindingBase {
  Boolean() : BindingBase(BindingType::Boolean, "boolean")
  {
    //
  }
  virtual BindingBase *clone() const override
  {
    return static_cast<BindingBase *>(new Boolean(*this));
  }
  virtual size_t getSize() const override
  {
    return sizeof(bool);
  }
};

template <typename T> struct Number : public BindingBase {
  using value_type = T;
  NumberType subtype;
  NumberFlags flags;

  Number(NumberType subtype, string name, NumberFlags flags = NumberFlags::None)
      : BindingBase(BindingType::Number, name), subtype(subtype), flags(flags)
  {
    //
  }
  Number(const Number &b) : BindingBase(b), subtype(b.subtype), flags(b.flags)
  {
    //
  }

  virtual BindingBase *clone() const override
  {
    return static_cast<BindingBase *>(new Number(*this));
  }
  virtual size_t getSize() const override
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
  const BindingBase *arrayType;
  size_t arraySize;

  Array(T *arrayType, size_t arraySize)
      : BindingBase(BindingType::Array, "array"), arrayType(arrayType),
        arraySize(arraySize)
  {
  }
  Array(const Array &b) : BindingBase(b), arrayType(b.arrayType), arraySize(b.arraySize)
  {
  }
  virtual BindingBase *clone() const override
  {
    return static_cast<BindingBase *>(new Array(*this));
  }
  virtual size_t getSize() const override
  {
    return arrayType->getSize() * arraySize;
  }
  virtual string buildFullName() const override
  {
    return arrayType->buildFullName() + "[" + std::to_string(arraySize) + "]";
  }
};

struct Pointer : public BindingBase {
  const BindingBase *ptrType;
  bool isNonNull = false;

  Pointer(const BindingBase *ptrType, string name = "pointer")
      : ptrType(ptrType), BindingBase(BindingType::Pointer, name)
  {
    //
  }

  Pointer(const Pointer &b) : BindingBase(b), ptrType(b.ptrType), isNonNull(b.isNonNull)
  {
    //
  }
  virtual BindingBase *clone() const override
  {
    return static_cast<BindingBase *>(new Pointer(*this));
  }
  virtual size_t getSize() const override
  {
    return sizeof(void *);
  }
  virtual string buildFullName() const override {
    return ptrType->buildFullName() + "*";
  }
};

struct Reference : public BindingBase {
  const BindingBase *refType;
  Reference(const BindingBase *ptrType, string name = "reference")
      : refType(ptrType), BindingBase(BindingType::Reference, name)
  {
    //
  }
  Reference(const Reference &p) : BindingBase(p), refType(p.refType)
  {
    //
  }
  virtual BindingBase *clone() const override
  {
    return static_cast<BindingBase *>(new Reference(*this));
  }
  virtual size_t getSize() const override
  {
    return sizeof(void *);
  }
  virtual string buildFullName() const override {
    return refType->buildFullName() + "&";
  }
};

} // namespace types
} // namespace litestl::binding
