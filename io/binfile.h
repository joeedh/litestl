/**

## Introduction 

the serialization system is a simple structured binary format
with schemas.  each class registers a schema which is used to
write files.  

the schema is a list of structs, each of which has a name.
the structs have fields, which also have names and also types.  
the fields are associated with an offset into the struct (this offset 
is runtime only).  note that struct is itself a field subtype.

the schema is written at the end of the file to facilitate version
migrations.

## Schema
Schemas are implemented with the StructSchema class.  Structs/classes
add themselves to the schema with addStruct().  Structs create their
schemas in a `defineSchema` static method.

## Schema field file layout
Note: the binary layout is designed to avoid padding bytes
inside the final structs.

Each field shares a common header:
* Field:
  - type: int32
  - name: char[508]

Other types are as follows:

* Struct:
  - structName: char[508]
  - id        : int32 // internal id
  - structSize: int32 // size of struct in bytes
  - fieldCount: int32
  - ...followed by fieldCount fields
* StructRef:
  - structName: char[508] // ref to original Struct
  - structSize: int32 // size of struct in bytes
  - id        : int32 // internal id of original Struct
* Array:
  - elementType  : Field // a field representing the type of array data
  - hasStaticSize: int32
  - staticSize   : int32 // number of elements in the array, UNLESS array has a static size
* Pointer:
  - pointerType  : Field
* Numeric types (integers, floats, etc):
  - no additional data
* String:
  - no additional data

## Writing files
Note: we support reading/writing to files with different endianness
A file is little endian if the first flag in the file header's flags is set,
otherwise it is big endian.

Steps to write a file:
* write header
* write number of top-level structs
* write top-level structs using file schema.
* serialize file schema
* write file offset of start of file schema

### Writing fields

* Struct
 - write struct id
 - write original struct pointer
 - write fields
* Array
 - write array count (unless array field has static size)
 - write original array pointer
 - write array data
* Pointer (and references)
 - write original pointer
* String:
 - write size of string
 - write string (without null-termination!)
* Numeric types:
 - write value

## File format 
File format:

magic            : 8 bytes
flags            : 1 byte, bit 0: little endian, bit 1: file data is compressed
version          : 3 bytes (major minor micro)
file data...     : X bytes
file schema data : X bytes
pointer to start
of file schema   : 8 bytes
*/

#pragma once

#include "binding/binding_base.h"
#include "litestl/binding/binding.h"
#include "litestl/util/map.h"
#include "litestl/util/string.h"
#include "litestl/util/vector.h"

#include <bit>
#include <cfloat>
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <type_traits>

namespace sculptcore::io {
using litestl::util::Map;
using litestl::util::string;
using litestl::util::stringref;
using litestl::util::Vector;
using std::iostream;

static constexpr bool hostLittleEndian = std::endian::native == std::endian::little;

enum class FileFlags {
  NONE = 0,
  LITTLE_ENDIAN = 1 << 0,
  COMPRESSED = 1 << 1,
};

struct FileHeader {
  char magic[8];
  uint8_t flags;
  uint8_t version_major;
  uint8_t version_minor;
  uint8_t version_micro;
};

enum class _FieldTypes {
  UINT8 = 0,
  INT8 = 1,
  UINT16 = 2,
  INT16 = 3,
  UINT32 = 4,
  INT32 = 5,
  UINT64 = 6,
  INT64 = 7,
  FLOAT = 8,
  DOUBLE = 9,
  STRING = 10,
  ARRAY = 11,
  STRUCT = 12,
  POINTER = 13,
};
MAKE_ENUM_CLASS(FieldTypes, _FieldTypes, uint32_t);

// we want to avoid padding bytes
struct FieldDef {
  char name[508];
  FieldTypes type;

  FieldDef() = default;
  FieldDef(const FieldDef &b) = default;
  FieldDef(FieldDef &&b) = default;
  FieldDef &operator=(const FieldDef &b) = default;
  FieldDef &operator=(FieldDef &&b) = default;
};

struct ArrayDef : public FieldDef {
  int isStatic;
  int staticSize;
  FieldDef *elemType;

  ArrayDef() = default;
  ArrayDef(const ArrayDef &b) = default;
  ArrayDef(const FieldDef &b) : FieldDef(b)
  {
  }
  ArrayDef(ArrayDef &&b) = default;
  ArrayDef &operator=(const ArrayDef &b) = default;
  ArrayDef &operator=(ArrayDef &&b) = default;
};

using litestl::binding::BindingBase;

struct IOStructDef : public FieldDef {

  char structName[508];
  uint32_t structSize;

  Vector<FieldDef *> fields;
  Vector<int> fieldOffsets;
  Map<string, int> fieldMap;

  IOStructDef() = default;
  IOStructDef(const IOStructDef &b) = default;
  IOStructDef(const FieldDef &b) : FieldDef(b)
  {
  }
  IOStructDef(IOStructDef &&b) = default;
  IOStructDef &operator=(const IOStructDef &b) = default;
  IOStructDef &operator=(IOStructDef &&b) = default;

  const FieldDef *getField(string name)
  {
    return fields[fieldMap[name]];
  }

  IOStructDef &add(FieldDef *field, int memberOffset)
  {
    fields.append(field);
    fieldOffsets.append(memberOffset);
    fieldMap[field->name] = fields.size() - 1;
    return *this;
  }

  template <std::integral T> IOStructDef &add(const char *name, int memberOffset)
  {
    FieldDef *def = new FieldDef();
    litestl::util::cstring::strNcpy(def->name, name);

    if constexpr (std::is_same_v<T, uint8_t>) {
      def->type = FieldTypes::UINT8;
    } else if constexpr (std::is_same_v<T, int8_t>) {
      def->type = FieldTypes::INT8;
    } else if constexpr (std::is_same_v<T, uint16_t>) {
      def->type = FieldTypes::UINT16;
    } else if constexpr (std::is_same_v<T, int16_t>) {
      def->type = FieldTypes::INT16;
    } else if constexpr (std::is_same_v<T, uint32_t>) {
      def->type = FieldTypes::UINT32;
    } else if constexpr (std::is_same_v<T, int32_t>) {
      def->type = FieldTypes::INT32;
    } else if constexpr (std::is_same_v<T, uint64_t>) {
      def->type = FieldTypes::UINT64;
    } else if constexpr (std::is_same_v<T, int64_t>) {
      def->type = FieldTypes::INT64;
    } else {
      static_assert(sizeof(T) == 0, "unsupported integral type");
    }

    return add(def, memberOffset);
  }

  template <std::floating_point T> IOStructDef &add(const char *name, int memberOffset)
  {
    FieldDef *def = new FieldDef();
    litestl::util::cstring::strNcpy(def->name, name);

    if constexpr (std::is_same_v<T, float>) {
      def->type = FieldTypes::FLOAT;
    } else if constexpr (std::is_same_v<T, double>) {
      def->type = FieldTypes::DOUBLE;
    } else {
      static_assert(sizeof(T) == 0, "unsupported integral type");
    }

    return add(def, memberOffset);
  }
};

template <typename CLS>
concept ClassIOReq = requires(IOStructDef *def) {
  { CLS::defineSchema() } -> std::convertible_to<IOStructDef *>;
  std::is_reference_v<CLS> == false;
};

template <typename CLS>
const IOStructDef *getIOStructDef()
  requires ClassIOReq<CLS>
{
  return CLS::defineSchema();
}

struct StructSchema {
  Map<string, IOStructDef *> structs;

  void addStruct(IOStructDef *def)
  {
    structs[def->name] = def;
  }
};

struct BinFile {
  bool littleEndian;
  Map<const char *, const char *> strTable;
  iostream &stream;

  FieldDef *readFieldDef()
  {
    FieldDef def;
    FieldDef *result = nullptr;
    stream.read(reinterpret_cast<char *>(&def), sizeof(def));

    switch (def.type) {
    case FieldTypes::ARRAY: {
      ArrayDef *arrayDef = new ArrayDef(def);
      arrayDef->elemType = readFieldDef();
      break;
    }
    case FieldTypes::STRUCT: {
      IOStructDef *structDef = new IOStructDef(def);
      readIOStructDef(structDef);
      break;
    }
    case FieldTypes::STRING:
      result = new FieldDef(def);
      break;
    }

    return result;
  }
  void readIOStructDef(IOStructDef *def)
  {
    uint32_t numFields = readUint32();
    for (uint32_t i = 0; i < numFields; i++) {
      FieldDef *f = readFieldDef();
      def->fieldMap[f->name] = i;
      def->fields.append(f);
    }
  }
  BinFile(iostream &stream) : littleEndian(true), stream(stream)
  {
  }

  template <typename T> inline constexpr void byteSwap(T &value)
  {
    if (littleEndian != hostLittleEndian) {
      char *c = reinterpret_cast<char *>(&value);
      int count = sizeof(T);
      for (int i = 0; i < (count >> 1); i++) {
        std::swap(c[i], c[count - i - 1]);
      }
    }
  }

  uint8_t readUint8()
  {
    uint8_t c;
    stream << c;
    return c;
  }
  int8_t readInt8()
  {
    int8_t c;
    stream << c;
    return c;
  }
  uint16_t readUint16()
  {
    uint16_t c;
    stream << c;
    byteSwap(c);
    return c;
  }
  int16_t readInt16()
  {
    int16_t c;
    stream << c;
    byteSwap(c);
    return c;
  }
  uint32_t readUint32()
  {
    uint32_t c;
    stream << c;
    byteSwap(c);
    return c;
  }
  int32_t readInt32()
  {
    int32_t c;
    stream << c;
    byteSwap(c);
    return c;
  }
  uint64_t readUint64()
  {
    uint64_t c;
    stream << c;
    byteSwap(c);
    return c;
  }
  int64_t readInt64()
  {
    int64_t c;
    stream << c;
    byteSwap(c);
    return c;
  }
  float readFloat()
  {
    float c;
    stream << c;
    byteSwap(c);
    return c;
  }
  double readDouble()
  {
    double c;
    stream << c;
    byteSwap(c);
    return c;
  }
  string readString()
  {
    uint32_t size = readUint32();
    char *buf;
    if (size < 512) {
      buf = static_cast<char *>(alloca(size + 1));
    } else {
      buf = new char[size + 1];
    }

    stream.read(buf, size);
    buf[size] = '\0';

    string s(buf);

    if (size < 512) {
      delete[] buf;
    }
    return s;
  }

  template <typename T>
  /*
   * Read a struct.  Note: dest is placement new constructed (not assigned).
   * Requires that structDef have an 'io' constructor that takes a BinFile*
   * as its sole argument;
   * as its sole argument;
   */
  void readStruct(T *dest, const litestl::binding::types::Struct<T> *structDef)
  {
    const auto *ctor = structDef->findConstructor("io");

    if (ctor == nullptr) {
      printf("no io constructor found for struct");
      abort();
    }

    void *args[1] = {static_cast<void *>(this)};
    ctor->thunk(dest, args, static_cast<void *>(dest));
  }

  /**
   * The elements in dest are placement new constructed, not assigned to.
   */
  template <typename T>
  void readArray(T *dest, const litestl::binding::BindingBase *elemType)
  {
    uint32_t size = readUint32();

    switch (elemType->type) {
    case litestl::binding::BindingType::Struct: {
      for (uint32_t i = 0; i < size; i++) {
        readStruct(dest + i,
                   static_cast<const litestl::binding::types::Struct<T> *>(elemType));
      }
      break;
    }
    case litestl::binding::BindingType::Array: {
      for (uint32_t i = 0; i < size; i++) {
        readArray(dest + i,
                  static_cast<const litestl::binding::types::Array<T> *>(elemType));
        break;
      }
    }
    }
  }

  BinFile &writeUint8(uint8_t value)
  {
    stream << value;
    return *this;
  }
  BinFile &writeInt8(int8_t value)
  {
    stream << value;
    return *this;
  }
  BinFile &writeUint16(uint16_t value)
  {
    byteSwap(value);
    stream.write(reinterpret_cast<const char *>(&value), sizeof(uint16_t));
    return *this;
  }
  BinFile &writeInt16(int16_t value)
  {
    byteSwap(value);
    stream.write(reinterpret_cast<const char *>(&value), sizeof(int16_t));
    return *this;
  }
  BinFile &writeUint32(uint32_t value)
  {
    byteSwap(value);
    stream.write(reinterpret_cast<const char *>(&value), sizeof(uint32_t));
    return *this;
  }
  BinFile &writeInt32(int32_t value)
  {
    byteSwap(value);
    stream.write(reinterpret_cast<const char *>(&value), sizeof(int32_t));
    return *this;
  }
  BinFile &writeUint64(uint64_t value)
  {
    byteSwap(value);
    stream.write(reinterpret_cast<const char *>(&value), sizeof(uint64_t));
    return *this;
  }
  BinFile &writeInt64(int64_t value)
  {
    byteSwap(value);
    stream.write(reinterpret_cast<const char *>(&value), sizeof(int64_t));
    return *this;
  }
  BinFile &writeFloat(float value)
  {
    byteSwap(value);
    stream.write(reinterpret_cast<const char *>(&value), sizeof(float));
    return *this;
  }
  BinFile &writeDouble(double value)
  {
    byteSwap(value);
    stream.write(reinterpret_cast<const char *>(&value), sizeof(double));
    return *this;
  }
  BinFile &writeString(const string &value)
  {
    writeUint32(value.size());
    stream.write(value.c_str(), value.size());
    return *this;
  }
};

} // namespace sculptcore::io