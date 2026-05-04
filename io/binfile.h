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
* Array:
  - elementType  : Field // a field representing the type of array data
  - hasStaticSize: int32
  - staticSize   : int32 // number of elements in the array, if hasStaticSize
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
 - write fields in order
* Array
 - write array count (unless array field has static size)
 - write array data
* Pointer (and references)
 - write original pointer (8 bytes)
* String:
 - write size of string
 - write string (without null-termination!)
* Numeric types:
 - write value

## File format
File format:

magic            : 8 bytes ("SCULPT00")
flags            : 1 byte, bit 0: little endian, bit 1: file data is compressed
version          : 3 bytes (major minor micro)
file data...     : X bytes
file schema data : X bytes
pointer to start
of file schema   : 8 bytes

## Reading files

Steps to read a file:
* read header
* read number of top-level structs
* read pointer to file schema (the last 8 bytes of the file)
* read file schema
* read top-level structs using file schema.

The structs are fully deserialized into a simple generic structure, where structs
are key/value stores of fields (note: internally the keys are field indices
not strings). 

```c++
struct FieldData {
  FieldType type;
}

struct StructData : public FieldData {
  StructDef *def.
  Vector<FieldData*> fields;

  FieldData *get(const char *name) {
    return fields[def->fieldMap[name]];
  }
  int8_t getInt8(const char *name) {
    return get(name)->getInt8();
  }
  int16_t getInt16(const char *name) {
    return get(name)->getInt16();
  }
  int32_t getInt32(const char *name) {
    return get(name)->getInt32();
  }
  int64_t getInt64(const char *name) {
    return get(name)->getInt64();
  }

  StructData *getStruct(const char *name) {
    return static_cast<StructData*>(get(name));
  }
}
```

Example of usage might be:

```
struct MyStruct {
  int32_t x;
  int32_t y;
  int32_t z;

  MyStruct(StructData *data) {
    x = data->getInt32("x");
    y = data->getInt32("y");
    z = data->getInt32("z");
  }
}
```

### Migrations

Note: the eventual migration system will transform this generic representation of the
file to match the latest version of the schema prior invoking any struct io constructors.

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
#include <functional>
#include <iostream>
#include <istream>
#include <ostream>
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

static constexpr char kMagic[8] = {'S', 'C', 'U', 'L', 'P', 'T', '0', '0'};

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

inline bool isNumeric(FieldTypes t)
{
  return uint32_t(_FieldTypes(t)) <= uint32_t(_FieldTypes::DOUBLE);
}
inline size_t numericSize(FieldTypes t)
{
  switch (_FieldTypes(t)) {
  case _FieldTypes::UINT8:
  case _FieldTypes::INT8:
    return 1;
  case _FieldTypes::UINT16:
  case _FieldTypes::INT16:
    return 2;
  case _FieldTypes::UINT32:
  case _FieldTypes::INT32:
  case _FieldTypes::FLOAT:
    return 4;
  case _FieldTypes::UINT64:
  case _FieldTypes::INT64:
  case _FieldTypes::DOUBLE:
    return 8;
  default:
    return 0;
  }
}

struct FieldDef {
  char name[508];
  FieldTypes type;

  FieldDef() : type(FieldTypes::UINT8)
  {
    name[0] = 0;
  }
  FieldDef(const FieldDef &b) = default;
  FieldDef(FieldDef &&b) = default;
  FieldDef &operator=(const FieldDef &b) = default;
  FieldDef &operator=(FieldDef &&b) = default;
  virtual ~FieldDef() = default;
};

struct ArrayDef : public FieldDef {
  int32_t isStatic;
  int32_t staticSize;
  FieldDef *elemType;

  ArrayDef() : isStatic(0), staticSize(0), elemType(nullptr)
  {
    type = FieldTypes::ARRAY;
  }
  ArrayDef(const ArrayDef &b) = default;
  ArrayDef(const FieldDef &b) : FieldDef(b), isStatic(0), staticSize(0), elemType(nullptr)
  {
  }
  ArrayDef(ArrayDef &&b) = default;
  ArrayDef &operator=(const ArrayDef &b) = default;
  ArrayDef &operator=(ArrayDef &&b) = default;
};

struct PointerDef : public FieldDef {
  FieldDef *pointeeType;

  PointerDef() : pointeeType(nullptr)
  {
    type = FieldTypes::POINTER;
  }
  PointerDef(const FieldDef &b) : FieldDef(b), pointeeType(nullptr)
  {
  }
};

using litestl::binding::BindingBase;

struct IOStructDef : public FieldDef {
  char structName[508];
  int32_t id;
  uint32_t structSize;

  Vector<FieldDef *> fields;
  Vector<int> fieldOffsets;
  Map<string, int> fieldMap;

  IOStructDef() : id(0), structSize(0)
  {
    type = FieldTypes::STRUCT;
    structName[0] = 0;
  }
  IOStructDef(const FieldDef &b) : FieldDef(b), id(0), structSize(0)
  {
    structName[0] = 0;
  }

  const FieldDef *getField(const string &fname)
  {
    int *idx = fieldMap.lookup_ptr(fname);
    if (!idx) {
      return nullptr;
    }
    return fields[*idx];
  }

  IOStructDef &add(FieldDef *field, int memberOffset)
  {
    fields.append(field);
    fieldOffsets.append(memberOffset);
    fieldMap[field->name] = fields.size() - 1;
    return *this;
  }

  template <std::integral T> IOStructDef &add(const char *fname, int memberOffset)
  {
    FieldDef *def = new FieldDef();
    litestl::util::cstring::strNcpy(def->name, fname);

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

  template <std::floating_point T> IOStructDef &add(const char *fname, int memberOffset)
  {
    FieldDef *def = new FieldDef();
    litestl::util::cstring::strNcpy(def->name, fname);

    if constexpr (std::is_same_v<T, float>) {
      def->type = FieldTypes::FLOAT;
    } else if constexpr (std::is_same_v<T, double>) {
      def->type = FieldTypes::DOUBLE;
    } else {
      static_assert(sizeof(T) == 0, "unsupported floating-point type");
    }

    return add(def, memberOffset);
  }

  /** Add a string field. */
  IOStructDef &addString(const char *fname, int memberOffset)
  {
    FieldDef *def = new FieldDef();
    litestl::util::cstring::strNcpy(def->name, fname);
    def->type = FieldTypes::STRING;
    return add(def, memberOffset);
  }

  /** Add a static-size array field with elements described by elemType.
   *  Takes ownership of elemType. */
  IOStructDef &addStaticArray(const char *fname,
                              int memberOffset,
                              FieldDef *elemType,
                              int staticSize)
  {
    ArrayDef *def = new ArrayDef();
    litestl::util::cstring::strNcpy(def->name, fname);
    def->isStatic = 1;
    def->staticSize = staticSize;
    def->elemType = elemType;
    return add(def, memberOffset);
  }

  /** Add a dynamic-size array field. The array length is written
   *  inline before the data. */
  IOStructDef &addDynamicArray(const char *fname, int memberOffset, FieldDef *elemType)
  {
    ArrayDef *def = new ArrayDef();
    litestl::util::cstring::strNcpy(def->name, fname);
    def->isStatic = 0;
    def->staticSize = 0;
    def->elemType = elemType;
    return add(def, memberOffset);
  }

  /** Add a pointer field — only the 8-byte original pointer value is
   *  written/read (we do not patch the address on read). */
  IOStructDef &addPointer(const char *fname, int memberOffset, FieldDef *pointeeType)
  {
    PointerDef *def = new PointerDef();
    litestl::util::cstring::strNcpy(def->name, fname);
    def->pointeeType = pointeeType;
    return add(def, memberOffset);
  }

  /** Embed a nested struct field. The child IOStructDef is owned by the parent. */
  IOStructDef &addNestedStruct(const char *fname, int memberOffset, IOStructDef *child)
  {
    litestl::util::cstring::strNcpy(child->name, fname);
    return add(child, memberOffset);
  }

  /** Construct a fresh, named IOStructDef for type T. Caller owns the result. */
  template <typename T> static IOStructDef *create(const char *typeName)
  {
    IOStructDef *def = new IOStructDef();
    litestl::util::cstring::strNcpy(def->structName, typeName);
    litestl::util::cstring::strNcpy(def->name, typeName);
    def->structSize = uint32_t(sizeof(T));
    return def;
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
  Vector<IOStructDef *> structs;
  Map<string, int> nameToIndex;

  ~StructSchema() = default;

  IOStructDef *addStruct(IOStructDef *def)
  {
    def->id = int32_t(structs.size());
    structs.append(def);
    nameToIndex[def->structName] = int(structs.size() - 1);
    return def;
  }

  IOStructDef *findByName(const string &n)
  {
    int *idx = nameToIndex.lookup_ptr(n);
    if (!idx) {
      return nullptr;
    }
    return structs[*idx];
  }
};

struct ArrayData;
struct StructData;

/**
 * Generic deserialized field. Holds either a primitive payload (numerics,
 * pointer) inline or a string payload, or — via subclasses — an array or
 * a nested struct. `type` is authoritative; accessors abort on mismatch.
 */
struct FieldData {
  FieldTypes type;
  union Prim {
    uint8_t u8;
    int8_t i8;
    uint16_t u16;
    int16_t i16;
    uint32_t u32;
    int32_t i32;
    uint64_t u64;
    int64_t i64;
    float f32;
    double f64;
    uint64_t pointer;
  } prim;
  string str;

  FieldData() : type(FieldTypes::UINT8), prim{}
  {
  }
  FieldData(const FieldData &) = delete;
  FieldData &operator=(const FieldData &) = delete;
  virtual ~FieldData() = default;

  uint8_t getUint8() const
  {
    if (type != FieldTypes::UINT8) {
      printf("FieldData::getUint8 type mismatch\n");
      abort();
    }
    return prim.u8;
  }
  int8_t getInt8() const
  {
    if (type != FieldTypes::INT8) {
      printf("FieldData::getInt8 type mismatch\n");
      abort();
    }
    return prim.i8;
  }
  uint16_t getUint16() const
  {
    if (type != FieldTypes::UINT16) {
      printf("FieldData::getUint16 type mismatch\n");
      abort();
    }
    return prim.u16;
  }
  int16_t getInt16() const
  {
    if (type != FieldTypes::INT16) {
      printf("FieldData::getInt16 type mismatch\n");
      abort();
    }
    return prim.i16;
  }
  uint32_t getUint32() const
  {
    if (type != FieldTypes::UINT32) {
      printf("FieldData::getUint32 type mismatch\n");
      abort();
    }
    return prim.u32;
  }
  int32_t getInt32() const
  {
    if (type != FieldTypes::INT32) {
      printf("FieldData::getInt32 type mismatch\n");
      abort();
    }
    return prim.i32;
  }
  uint64_t getUint64() const
  {
    if (type != FieldTypes::UINT64) {
      printf("FieldData::getUint64 type mismatch\n");
      abort();
    }
    return prim.u64;
  }
  int64_t getInt64() const
  {
    if (type != FieldTypes::INT64) {
      printf("FieldData::getInt64 type mismatch\n");
      abort();
    }
    return prim.i64;
  }
  float getFloat() const
  {
    if (type != FieldTypes::FLOAT) {
      printf("FieldData::getFloat type mismatch\n");
      abort();
    }
    return prim.f32;
  }
  double getDouble() const
  {
    if (type != FieldTypes::DOUBLE) {
      printf("FieldData::getDouble type mismatch\n");
      abort();
    }
    return prim.f64;
  }
  const string &getString() const
  {
    if (type != FieldTypes::STRING) {
      printf("FieldData::getString type mismatch\n");
      abort();
    }
    return str;
  }
  uint64_t getPointer() const
  {
    if (type != FieldTypes::POINTER) {
      printf("FieldData::getPointer type mismatch\n");
      abort();
    }
    return prim.pointer;
  }
};

struct ArrayData : FieldData {
  Vector<FieldData *> elements;

  ArrayData()
  {
    type = FieldTypes::ARRAY;
  }
  ~ArrayData() override
  {
    for (FieldData *e : elements) {
      delete e;
    }
  }
};

/**
 * Generic deserialized struct. `def` describes the file-side schema; `fields`
 * is parallel to `def->fields` and owned. Lookup by name forwards through
 * `def->fieldMap`.
 */
struct StructData : FieldData {
  const IOStructDef *def = nullptr;
  Vector<FieldData *> fields;

  StructData()
  {
    type = FieldTypes::STRUCT;
  }
  ~StructData() override
  {
    for (FieldData *f : fields) {
      delete f;
    }
  }

  FieldData *get(const char *name) const
  {
    if (!def) {
      return nullptr;
    }
    int *idx = const_cast<IOStructDef *>(def)->fieldMap.lookup_ptr(string(name));
    if (!idx || *idx < 0 || size_t(*idx) >= fields.size()) {
      return nullptr;
    }
    return fields[*idx];
  }

  FieldData *getOrAbort(const char *name) const
  {
    FieldData *fd = get(name);
    if (!fd) {
      printf("StructData: missing field '%s'\n", name);
      abort();
    }
    return fd;
  }

  uint8_t getUint8(const char *n) const
  {
    return getOrAbort(n)->getUint8();
  }
  int8_t getInt8(const char *n) const
  {
    return getOrAbort(n)->getInt8();
  }
  uint16_t getUint16(const char *n) const
  {
    return getOrAbort(n)->getUint16();
  }
  int16_t getInt16(const char *n) const
  {
    return getOrAbort(n)->getInt16();
  }
  uint32_t getUint32(const char *n) const
  {
    return getOrAbort(n)->getUint32();
  }
  int32_t getInt32(const char *n) const
  {
    return getOrAbort(n)->getInt32();
  }
  uint64_t getUint64(const char *n) const
  {
    return getOrAbort(n)->getUint64();
  }
  int64_t getInt64(const char *n) const
  {
    return getOrAbort(n)->getInt64();
  }
  float getFloat(const char *n) const
  {
    return getOrAbort(n)->getFloat();
  }
  double getDouble(const char *n) const
  {
    return getOrAbort(n)->getDouble();
  }
  const string &getString(const char *n) const
  {
    return getOrAbort(n)->getString();
  }
  uint64_t getPointer(const char *n) const
  {
    return getOrAbort(n)->getPointer();
  }

  StructData *getStruct(const char *n) const
  {
    FieldData *fd = get(n);
    if (!fd || fd->type != FieldTypes::STRUCT) {
      return nullptr;
    }
    return static_cast<StructData *>(fd);
  }
  ArrayData *getArray(const char *n) const
  {
    FieldData *fd = get(n);
    if (!fd || fd->type != FieldTypes::ARRAY) {
      return nullptr;
    }
    return static_cast<ArrayData *>(fd);
  }
};

struct BinFile {
  bool littleEndian;
  uint8_t version[3];
  iostream &stream;

  BinFile(iostream &stream)
      : littleEndian(hostLittleEndian), version{0, 0, 1}, stream(stream)
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

  // ---------------- primitive read ----------------

  uint8_t readUint8()
  {
    uint8_t c = 0;
    stream.read(reinterpret_cast<char *>(&c), 1);
    return c;
  }
  int8_t readInt8()
  {
    int8_t c = 0;
    stream.read(reinterpret_cast<char *>(&c), 1);
    return c;
  }
  uint16_t readUint16()
  {
    uint16_t c = 0;
    stream.read(reinterpret_cast<char *>(&c), 2);
    byteSwap(c);
    return c;
  }
  int16_t readInt16()
  {
    int16_t c = 0;
    stream.read(reinterpret_cast<char *>(&c), 2);
    byteSwap(c);
    return c;
  }
  uint32_t readUint32()
  {
    uint32_t c = 0;
    stream.read(reinterpret_cast<char *>(&c), 4);
    byteSwap(c);
    return c;
  }
  int32_t readInt32()
  {
    int32_t c = 0;
    stream.read(reinterpret_cast<char *>(&c), 4);
    byteSwap(c);
    return c;
  }
  uint64_t readUint64()
  {
    uint64_t c = 0;
    stream.read(reinterpret_cast<char *>(&c), 8);
    byteSwap(c);
    return c;
  }
  int64_t readInt64()
  {
    int64_t c = 0;
    stream.read(reinterpret_cast<char *>(&c), 8);
    byteSwap(c);
    return c;
  }
  float readFloat()
  {
    float c = 0;
    stream.read(reinterpret_cast<char *>(&c), 4);
    byteSwap(c);
    return c;
  }
  double readDouble()
  {
    double c = 0;
    stream.read(reinterpret_cast<char *>(&c), 8);
    byteSwap(c);
    return c;
  }

  string readString()
  {
    uint32_t size = readUint32();
    if (size == 0) {
      return string("");
    }

    bool onHeap = size >= 512;
    char *buf = onHeap ? new char[size + 1] : static_cast<char *>(alloca(size + 1));
    stream.read(buf, size);
    buf[size] = '\0';

    string s(buf);
    if (onHeap) {
      delete[] buf;
    }
    return s;
  }

  // ---------------- primitive write ----------------

  BinFile &writeUint8(uint8_t value)
  {
    stream.write(reinterpret_cast<const char *>(&value), 1);
    return *this;
  }
  BinFile &writeInt8(int8_t value)
  {
    stream.write(reinterpret_cast<const char *>(&value), 1);
    return *this;
  }
  BinFile &writeUint16(uint16_t value)
  {
    byteSwap(value);
    stream.write(reinterpret_cast<const char *>(&value), 2);
    return *this;
  }
  BinFile &writeInt16(int16_t value)
  {
    byteSwap(value);
    stream.write(reinterpret_cast<const char *>(&value), 2);
    return *this;
  }
  BinFile &writeUint32(uint32_t value)
  {
    byteSwap(value);
    stream.write(reinterpret_cast<const char *>(&value), 4);
    return *this;
  }
  BinFile &writeInt32(int32_t value)
  {
    byteSwap(value);
    stream.write(reinterpret_cast<const char *>(&value), 4);
    return *this;
  }
  BinFile &writeUint64(uint64_t value)
  {
    byteSwap(value);
    stream.write(reinterpret_cast<const char *>(&value), 8);
    return *this;
  }
  BinFile &writeInt64(int64_t value)
  {
    byteSwap(value);
    stream.write(reinterpret_cast<const char *>(&value), 8);
    return *this;
  }
  BinFile &writeFloat(float value)
  {
    byteSwap(value);
    stream.write(reinterpret_cast<const char *>(&value), 4);
    return *this;
  }
  BinFile &writeDouble(double value)
  {
    byteSwap(value);
    stream.write(reinterpret_cast<const char *>(&value), 8);
    return *this;
  }
  BinFile &writeString(const string &value)
  {
    writeUint32(uint32_t(value.size()));
    if (value.size() > 0) {
      stream.write(value.c_str(), value.size());
    }
    return *this;
  }
  BinFile &writeString(const char *value)
  {
    return writeString(string(value));
  }

  // ---------------- header ----------------

  void writeHeader()
  {
    stream.write(kMagic, 8);
    uint8_t flags = 0;
    if (littleEndian) {
      flags |= uint8_t(FileFlags::LITTLE_ENDIAN);
    }
    stream.write(reinterpret_cast<const char *>(&flags), 1);
    stream.write(reinterpret_cast<const char *>(&version[0]), 1);
    stream.write(reinterpret_cast<const char *>(&version[1]), 1);
    stream.write(reinterpret_cast<const char *>(&version[2]), 1);
  }

  bool readHeader()
  {
    char magic[8] = {0};
    stream.read(magic, 8);
    for (int i = 0; i < 8; i++) {
      if (magic[i] != kMagic[i]) {
        return false;
      }
    }
    uint8_t flags = 0;
    stream.read(reinterpret_cast<char *>(&flags), 1);
    littleEndian = (flags & uint8_t(FileFlags::LITTLE_ENDIAN)) != 0;
    stream.read(reinterpret_cast<char *>(&version[0]), 1);
    stream.read(reinterpret_cast<char *>(&version[1]), 1);
    stream.read(reinterpret_cast<char *>(&version[2]), 1);
    return true;
  }

  // ---------------- field schema serialize ----------------

  /**
   * Serialize a field def. Recurses for ARRAY/POINTER/STRUCT subtypes.
   * Format: int32 type, char[508] name, then type-specific tail.
   */
  void writeFieldDef(const FieldDef *def)
  {
    writeUint32(uint32_t(_FieldTypes(def->type)));
    stream.write(def->name, 508);

    switch (_FieldTypes(def->type)) {
    case _FieldTypes::ARRAY: {
      const ArrayDef *ad = static_cast<const ArrayDef *>(def);
      writeFieldDef(ad->elemType);
      writeInt32(ad->isStatic);
      writeInt32(ad->staticSize);
      break;
    }
    case _FieldTypes::POINTER: {
      const PointerDef *pd = static_cast<const PointerDef *>(def);
      writeFieldDef(pd->pointeeType);
      break;
    }
    case _FieldTypes::STRUCT: {
      const IOStructDef *sd = static_cast<const IOStructDef *>(def);
      stream.write(sd->structName, 508);
      writeInt32(sd->id);
      writeUint32(sd->structSize);
      writeUint32(uint32_t(sd->fields.size()));
      for (const FieldDef *child : sd->fields) {
        writeFieldDef(child);
      }
      break;
    }
    default:
      break;
    }
  }

  /**
   * Read a field def. Returns a heap-allocated FieldDef (or subclass).
   * Caller takes ownership.
   */
  FieldDef *readFieldDef()
  {
    uint32_t typeRaw = readUint32();
    char nameBuf[508];
    stream.read(nameBuf, 508);

    _FieldTypes ftype = _FieldTypes(typeRaw);

    switch (ftype) {
    case _FieldTypes::UINT8:
    case _FieldTypes::INT8:
    case _FieldTypes::UINT16:
    case _FieldTypes::INT16:
    case _FieldTypes::UINT32:
    case _FieldTypes::INT32:
    case _FieldTypes::UINT64:
    case _FieldTypes::INT64:
    case _FieldTypes::FLOAT:
    case _FieldTypes::DOUBLE:
    case _FieldTypes::STRING: {
      FieldDef *def = new FieldDef();
      def->type = ftype;
      std::memcpy(def->name, nameBuf, 508);
      return def;
    }
    case _FieldTypes::ARRAY: {
      ArrayDef *def = new ArrayDef();
      def->type = ftype;
      std::memcpy(def->name, nameBuf, 508);
      def->elemType = readFieldDef();
      def->isStatic = readInt32();
      def->staticSize = readInt32();
      return def;
    }
    case _FieldTypes::POINTER: {
      PointerDef *def = new PointerDef();
      def->type = ftype;
      std::memcpy(def->name, nameBuf, 508);
      def->pointeeType = readFieldDef();
      return def;
    }
    case _FieldTypes::STRUCT: {
      IOStructDef *def = new IOStructDef();
      def->type = ftype;
      std::memcpy(def->name, nameBuf, 508);
      stream.read(def->structName, 508);
      def->id = readInt32();
      def->structSize = readUint32();
      uint32_t fieldCount = readUint32();
      for (uint32_t i = 0; i < fieldCount; i++) {
        FieldDef *child = readFieldDef();
        def->fieldMap[child->name] = int(i);
        def->fields.append(child);
        def->fieldOffsets.append(0); // offsets are runtime-only
      }
      return def;
    }
    }
    return nullptr;
  }

  // ---------------- whole schema serialize ----------------

  void writeSchema(const StructSchema &schema)
  {
    writeUint32(uint32_t(schema.structs.size()));
    for (const IOStructDef *s : schema.structs) {
      writeFieldDef(s);
    }
  }

  void readSchema(StructSchema &out)
  {
    uint32_t n = readUint32();
    for (uint32_t i = 0; i < n; i++) {
      FieldDef *fd = readFieldDef();
      if (!fd || fd->type != FieldTypes::STRUCT) {
        delete fd;
        continue;
      }
      out.addStruct(static_cast<IOStructDef *>(fd));
    }
  }

  // ---------------- struct/array data (schema-driven) ----------------

  void writeFieldData(const void *data, const FieldDef *def);
  void readFieldData(void *data, const FieldDef *def);

  void writeStructBySchema(const void *data, const IOStructDef *def)
  {
    const char *base = static_cast<const char *>(data);
    for (size_t i = 0; i < def->fields.size(); i++) {
      writeFieldData(base + def->fieldOffsets[i], def->fields[i]);
    }
  }

  void readStructBySchema(void *data, const IOStructDef *def)
  {
    char *base = static_cast<char *>(data);
    for (size_t i = 0; i < def->fields.size(); i++) {
      readFieldData(base + def->fieldOffsets[i], def->fields[i]);
    }
  }

  void writeArrayBySchema(const void *base, const ArrayDef *def, uint32_t count)
  {
    if (!def->isStatic) {
      writeUint32(count);
    }
    size_t elemSize = elemTypeSize(def->elemType);
    const char *p = static_cast<const char *>(base);
    for (uint32_t i = 0; i < count; i++) {
      writeFieldData(p, def->elemType);
      p += elemSize;
    }
  }

  uint32_t readArrayBySchema(void *base, const ArrayDef *def)
  {
    uint32_t count = def->isStatic ? uint32_t(def->staticSize) : readUint32();
    size_t elemSize = elemTypeSize(def->elemType);
    char *p = static_cast<char *>(base);
    for (uint32_t i = 0; i < count; i++) {
      readFieldData(p, def->elemType);
      p += elemSize;
    }
    return count;
  }

  /** Best-effort element stride for an array element. Used for static arrays. */
  static size_t elemTypeSize(const FieldDef *def)
  {
    switch (_FieldTypes(def->type)) {
    case _FieldTypes::UINT8:
    case _FieldTypes::INT8:
      return 1;
    case _FieldTypes::UINT16:
    case _FieldTypes::INT16:
      return 2;
    case _FieldTypes::UINT32:
    case _FieldTypes::INT32:
    case _FieldTypes::FLOAT:
      return 4;
    case _FieldTypes::UINT64:
    case _FieldTypes::INT64:
    case _FieldTypes::DOUBLE:
      return 8;
    case _FieldTypes::POINTER:
      return sizeof(void *);
    case _FieldTypes::STRUCT:
      return static_cast<const IOStructDef *>(def)->structSize;
    default:
      return 0;
    }
  }

  // ---------------- generic deserialize (StructData intermediate form) ----

  /**
   * Read one field according to `def` and return a freshly-allocated
   * FieldData tree. Recurses for ARRAY/STRUCT. Caller owns the result.
   */
  FieldData *readFieldDataGeneric(const FieldDef *def);

  /**
   * Read a complete struct as a generic StructData. Caller owns the result.
   * `def` must point at a struct type and remain alive for the lifetime of
   * the returned StructData (StructData stores a non-owning pointer to it).
   */
  StructData *readStructData(const IOStructDef *def)
  {
    StructData *out = new StructData();
    out->def = def;
    for (size_t i = 0; i < def->fields.size(); i++) {
      out->fields.append(readFieldDataGeneric(def->fields[i]));
    }
    return out;
  }

  // ---------------- binding-driven struct read ----------------

  /**
   * Read a struct via the binding system's "io" constructor.
   *
   * The flow is: deserialize the bytes into a generic `StructData` using
   * the file-side `def`, then invoke the "io" constructor with that
   * StructData* as its sole argument. A future migration pass will sit
   * between these two steps. `dest` is placement-new constructed.
   */
  template <typename T>
  void readStruct(T *dest,
                  const IOStructDef *def,
                  const litestl::binding::types::Struct<T> *structDef)
  {
    const auto *ctor = structDef->findConstructor("io");
    if (ctor == nullptr) {
      printf("no io constructor found for struct %s\n", structDef->name.c_str());
      abort();
    }
    StructData *sd = readStructData(def);
    void *args[1] = {static_cast<void *>(&sd)};
    ctor->thunk(static_cast<void *>(dest), args);
    delete sd;
  }

  // ---------------- file-level write/read ----------------

  /**
   * Write a complete file: header, callback (which writes the top-level
   * payload), schema at the end, and an 8-byte trailing offset to the
   * schema start.
   */
  void writeFile(const StructSchema &schema, const std::function<void(BinFile &)> &payload)
  {
    writeHeader();
    payload(*this);
    int64_t schemaOffset = int64_t(stream.tellp());
    writeSchema(schema);
    writeInt64(schemaOffset);
  }

  /**
   * Read a complete file: header, then jump to the trailing offset to
   * read the schema, then return to right after the header and invoke
   * the payload callback.
   */
  bool readFile(StructSchema &outSchema, const std::function<void(BinFile &)> &payload)
  {
    if (!readHeader()) {
      return false;
    }
    auto headerEnd = stream.tellg();

    stream.seekg(-int(sizeof(int64_t)), std::ios::end);
    int64_t schemaOffset = readInt64();

    stream.seekg(schemaOffset);
    readSchema(outSchema);

    stream.seekg(headerEnd);
    payload(*this);
    return true;
  }
};

inline void BinFile::writeFieldData(const void *data, const FieldDef *def)
{
  switch (_FieldTypes(def->type)) {
  case _FieldTypes::UINT8:
    writeUint8(*static_cast<const uint8_t *>(data));
    break;
  case _FieldTypes::INT8:
    writeInt8(*static_cast<const int8_t *>(data));
    break;
  case _FieldTypes::UINT16:
    writeUint16(*static_cast<const uint16_t *>(data));
    break;
  case _FieldTypes::INT16:
    writeInt16(*static_cast<const int16_t *>(data));
    break;
  case _FieldTypes::UINT32:
    writeUint32(*static_cast<const uint32_t *>(data));
    break;
  case _FieldTypes::INT32:
    writeInt32(*static_cast<const int32_t *>(data));
    break;
  case _FieldTypes::UINT64:
    writeUint64(*static_cast<const uint64_t *>(data));
    break;
  case _FieldTypes::INT64:
    writeInt64(*static_cast<const int64_t *>(data));
    break;
  case _FieldTypes::FLOAT:
    writeFloat(*static_cast<const float *>(data));
    break;
  case _FieldTypes::DOUBLE:
    writeDouble(*static_cast<const double *>(data));
    break;
  case _FieldTypes::STRING:
    writeString(*static_cast<const string *>(data));
    break;
  case _FieldTypes::POINTER: {
    uintptr_t p = reinterpret_cast<uintptr_t>(*static_cast<void *const *>(data));
    writeUint64(uint64_t(p));
    break;
  }
  case _FieldTypes::STRUCT: {
    writeStructBySchema(data, static_cast<const IOStructDef *>(def));
    break;
  }
  case _FieldTypes::ARRAY: {
    const ArrayDef *ad = static_cast<const ArrayDef *>(def);
    if (!ad->isStatic) {
      printf("dynamic arrays not supported in writeFieldData\n");
      abort();
    }
    writeArrayBySchema(data, ad, uint32_t(ad->staticSize));
    break;
  }
  }
}

inline void BinFile::readFieldData(void *data, const FieldDef *def)
{
  switch (_FieldTypes(def->type)) {
  case _FieldTypes::UINT8:
    *static_cast<uint8_t *>(data) = readUint8();
    break;
  case _FieldTypes::INT8:
    *static_cast<int8_t *>(data) = readInt8();
    break;
  case _FieldTypes::UINT16:
    *static_cast<uint16_t *>(data) = readUint16();
    break;
  case _FieldTypes::INT16:
    *static_cast<int16_t *>(data) = readInt16();
    break;
  case _FieldTypes::UINT32:
    *static_cast<uint32_t *>(data) = readUint32();
    break;
  case _FieldTypes::INT32:
    *static_cast<int32_t *>(data) = readInt32();
    break;
  case _FieldTypes::UINT64:
    *static_cast<uint64_t *>(data) = readUint64();
    break;
  case _FieldTypes::INT64:
    *static_cast<int64_t *>(data) = readInt64();
    break;
  case _FieldTypes::FLOAT:
    *static_cast<float *>(data) = readFloat();
    break;
  case _FieldTypes::DOUBLE:
    *static_cast<double *>(data) = readDouble();
    break;
  case _FieldTypes::STRING:
    *static_cast<string *>(data) = readString();
    break;
  case _FieldTypes::POINTER: {
    uint64_t p = readUint64();
    *static_cast<void **>(data) = reinterpret_cast<void *>(uintptr_t(p));
    break;
  }
  case _FieldTypes::STRUCT: {
    readStructBySchema(data, static_cast<const IOStructDef *>(def));
    break;
  }
  case _FieldTypes::ARRAY: {
    const ArrayDef *ad = static_cast<const ArrayDef *>(def);
    if (!ad->isStatic) {
      printf("dynamic arrays not supported in readFieldData\n");
      abort();
    }
    readArrayBySchema(data, ad);
    break;
  }
  }
}

inline FieldData *BinFile::readFieldDataGeneric(const FieldDef *def)
{
  switch (_FieldTypes(def->type)) {
  case _FieldTypes::UINT8: {
    auto *fd = new FieldData();
    fd->type = FieldTypes::UINT8;
    fd->prim.u8 = readUint8();
    return fd;
  }
  case _FieldTypes::INT8: {
    auto *fd = new FieldData();
    fd->type = FieldTypes::INT8;
    fd->prim.i8 = readInt8();
    return fd;
  }
  case _FieldTypes::UINT16: {
    auto *fd = new FieldData();
    fd->type = FieldTypes::UINT16;
    fd->prim.u16 = readUint16();
    return fd;
  }
  case _FieldTypes::INT16: {
    auto *fd = new FieldData();
    fd->type = FieldTypes::INT16;
    fd->prim.i16 = readInt16();
    return fd;
  }
  case _FieldTypes::UINT32: {
    auto *fd = new FieldData();
    fd->type = FieldTypes::UINT32;
    fd->prim.u32 = readUint32();
    return fd;
  }
  case _FieldTypes::INT32: {
    auto *fd = new FieldData();
    fd->type = FieldTypes::INT32;
    fd->prim.i32 = readInt32();
    return fd;
  }
  case _FieldTypes::UINT64: {
    auto *fd = new FieldData();
    fd->type = FieldTypes::UINT64;
    fd->prim.u64 = readUint64();
    return fd;
  }
  case _FieldTypes::INT64: {
    auto *fd = new FieldData();
    fd->type = FieldTypes::INT64;
    fd->prim.i64 = readInt64();
    return fd;
  }
  case _FieldTypes::FLOAT: {
    auto *fd = new FieldData();
    fd->type = FieldTypes::FLOAT;
    fd->prim.f32 = readFloat();
    return fd;
  }
  case _FieldTypes::DOUBLE: {
    auto *fd = new FieldData();
    fd->type = FieldTypes::DOUBLE;
    fd->prim.f64 = readDouble();
    return fd;
  }
  case _FieldTypes::STRING: {
    auto *fd = new FieldData();
    fd->type = FieldTypes::STRING;
    fd->str = readString();
    return fd;
  }
  case _FieldTypes::POINTER: {
    auto *fd = new FieldData();
    fd->type = FieldTypes::POINTER;
    fd->prim.pointer = readUint64();
    return fd;
  }
  case _FieldTypes::ARRAY: {
    const ArrayDef *ad = static_cast<const ArrayDef *>(def);
    if (!ad->isStatic) {
      printf("dynamic arrays not supported in readFieldDataGeneric\n");
      abort();
    }
    auto *out = new ArrayData();
    uint32_t count = uint32_t(ad->staticSize);
    for (uint32_t i = 0; i < count; i++) {
      out->elements.append(readFieldDataGeneric(ad->elemType));
    }
    return out;
  }
  case _FieldTypes::STRUCT: {
    const IOStructDef *sd = static_cast<const IOStructDef *>(def);
    auto *out = new StructData();
    out->def = sd;
    for (size_t i = 0; i < sd->fields.size(); i++) {
      out->fields.append(readFieldDataGeneric(sd->fields[i]));
    }
    return out;
  }
  }
  return nullptr;
}

} // namespace sculptcore::io
