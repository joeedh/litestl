#pragma once

#include "util/compiler_util.h"
#include "util/string.h"

namespace litestl::binding {
using util::string;

enum class BindingType {
  Boolean = 1 << 0,           // 1
  Number = 1 << 1,            // 2
  Pointer = 1 << 2,           // 4
  Reference = 1 << 3,         // 8
  Struct = 1 << 4,            // 16
  Array = 1 << 5,             // 32
  Method = 1 << 6,            // 64
  Literal = 1 << 7,           // 128
  Constructor = 1 << 8,       // 256
  Enum = 1 << 9,              // 512
  Union = 1 << 10,            // 1024
  ParentTemplParam = 1 << 11, // 2048
};
FlagOperators(BindingType);

enum class NumberType {
  Int8 = 1 << 0,    // 1
  Int16 = 1 << 1,   // 2
  Int32 = 1 << 2,   // 4
  Int64 = 1 << 3,   // 8
  Float32 = 1 << 4, // 16
  Float64 = 1 << 5, // 32
};
FlagOperators(NumberType);
enum class NumberFlags {
  None = 0, //
  Unsigned = 1 << 0
};
FlagOperators(NumberFlags);

struct BindingBase {
  BindingType type;
  string name;

  BindingBase(BindingType type, string name) : type(type), name(name)
  {
    //
  }
  BindingBase(const BindingBase &b) : type(b.type), name(b.name)
  {
  }
  virtual BindingBase *clone()
  {
    return nullptr;
  }
  virtual size_t getSize() const
  {
    return 0;
  }
  /** Creates a full name including template parameters. */
  virtual string buildFullName() const
  {
    return name;
  }
};

} // namespace litestl::binding
