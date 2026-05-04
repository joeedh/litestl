#include "io/binfile.h"
#include "test_util.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <sstream>

test_init;

using namespace litestl;
using namespace sculptcore::io;
using litestl::binding::types::Struct;

// ------------------------------------------------------------------
// Dummy types covering the documented field types.
// ------------------------------------------------------------------

struct AllNumerics {
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

  static IOStructDef *defineSchema()
  {
    IOStructDef *def = IOStructDef::create<AllNumerics>("AllNumerics");
    def->add<uint8_t>("u8", offsetof(AllNumerics, u8));
    def->add<int8_t>("i8", offsetof(AllNumerics, i8));
    def->add<uint16_t>("u16", offsetof(AllNumerics, u16));
    def->add<int16_t>("i16", offsetof(AllNumerics, i16));
    def->add<uint32_t>("u32", offsetof(AllNumerics, u32));
    def->add<int32_t>("i32", offsetof(AllNumerics, i32));
    def->add<uint64_t>("u64", offsetof(AllNumerics, u64));
    def->add<int64_t>("i64", offsetof(AllNumerics, i64));
    def->add<float>("f32", offsetof(AllNumerics, f32));
    def->add<double>("f64", offsetof(AllNumerics, f64));
    return def;
  }
};

struct WithString {
  util::string s;

  static IOStructDef *defineSchema()
  {
    IOStructDef *def = IOStructDef::create<WithString>("WithString");
    def->addString("s", offsetof(WithString, s));
    return def;
  }
};

struct StaticArrayHolder {
  int32_t fixed[4];

  static IOStructDef *defineSchema()
  {
    IOStructDef *def = IOStructDef::create<StaticArrayHolder>("StaticArrayHolder");
    FieldDef *elem = new FieldDef();
    elem->type = FieldTypes::INT32;
    util::cstring::strNcpy(elem->name, "elem");
    def->addStaticArray("fixed", offsetof(StaticArrayHolder, fixed), elem, 4);
    return def;
  }
};

struct Inner {
  int32_t x;
  float y;

  static IOStructDef *defineSchema()
  {
    IOStructDef *def = IOStructDef::create<Inner>("Inner");
    def->add<int32_t>("x", offsetof(Inner, x));
    def->add<float>("y", offsetof(Inner, y));
    return def;
  }
};

struct Outer {
  int32_t tag;
  Inner inner;

  static IOStructDef *defineSchema()
  {
    IOStructDef *def = IOStructDef::create<Outer>("Outer");
    def->add<int32_t>("tag", offsetof(Outer, tag));
    def->addNestedStruct("inner", offsetof(Outer, inner), Inner::defineSchema());
    return def;
  }
};

struct PointerHolder {
  int32_t *raw;

  static IOStructDef *defineSchema()
  {
    IOStructDef *def = IOStructDef::create<PointerHolder>("PointerHolder");
    FieldDef *pointee = new FieldDef();
    pointee->type = FieldTypes::INT32;
    util::cstring::strNcpy(pointee->name, "pointee");
    def->addPointer("raw", offsetof(PointerHolder, raw), pointee);
    return def;
  }
};

// A struct that uses the binding system + an "io" constructor.
struct BoundIO {
  int32_t a;
  int32_t b;

  BoundIO() : a(0), b(0)
  {
  }
  BoundIO(StructData *sd)
  {
    a = sd->getInt32("a");
    b = sd->getInt32("b");
  }

  static IOStructDef *defineSchema()
  {
    IOStructDef *def = IOStructDef::create<BoundIO>("BoundIO");
    def->add<int32_t>("a", offsetof(BoundIO, a));
    def->add<int32_t>("b", offsetof(BoundIO, b));
    return def;
  }

  static binding::types::Struct<BoundIO> *defineBindings()
  {
    alloc::PermanentGuard guard;
    using binding::types::Struct;
    Struct<BoundIO> *st = new Struct<BoundIO>("test::BoundIO", sizeof(BoundIO));
    BIND_STRUCT_DEFAULT_CONSTRUCTOR(st);

    // Manually build the "io" constructor — Bind<StructData*>() has no
    // specialization, so we cannot use BIND_STRUCT_CONSTRUCTOR here.
    auto *ioCtor = new binding::types::Constructor("io");
    ioCtor->ownerType = st;
    ioCtor->thunk = [](void *outBuf, void **args) {
      StructData *sd = *static_cast<StructData **>(args[0]);
      ::new (outBuf) BoundIO(sd);
    };
    st->addConstructor(ioCtor);
    return st;
  }
};

// ------------------------------------------------------------------
// Helpers
// ------------------------------------------------------------------

static void deleteSchema(IOStructDef *def);

static void deleteFieldDef(FieldDef *fd)
{
  if (!fd) {
    return;
  }
  switch (_FieldTypes(fd->type)) {
  case _FieldTypes::ARRAY: {
    ArrayDef *ad = static_cast<ArrayDef *>(fd);
    deleteFieldDef(ad->elemType);
    delete ad;
    break;
  }
  case _FieldTypes::POINTER: {
    PointerDef *pd = static_cast<PointerDef *>(fd);
    deleteFieldDef(pd->pointeeType);
    delete pd;
    break;
  }
  case _FieldTypes::STRUCT:
    deleteSchema(static_cast<IOStructDef *>(fd));
    break;
  default:
    delete fd;
    break;
  }
}

static void deleteSchema(IOStructDef *def)
{
  if (!def) {
    return;
  }
  for (FieldDef *fd : def->fields) {
    deleteFieldDef(fd);
  }
  delete def;
}

// ------------------------------------------------------------------
// Tests
// ------------------------------------------------------------------

static void test_numeric_roundtrip(bool flipEndian)
{
  std::stringstream ss;
  BinFile bf(ss);
  if (flipEndian) {
    bf.littleEndian = !sculptcore::io::hostLittleEndian;
  }

  bf.writeUint8(0xAB);
  bf.writeInt8(-7);
  bf.writeUint16(0xBEEF);
  bf.writeInt16(-12345);
  bf.writeUint32(0xDEADBEEFu);
  bf.writeInt32(-987654321);
  bf.writeUint64(0x0123456789ABCDEFull);
  bf.writeInt64(-1234567890123456789ll);
  bf.writeFloat(3.5f);
  bf.writeDouble(-2.71828);

  test_assert(bf.readUint8() == 0xAB);
  test_assert(bf.readInt8() == -7);
  test_assert(bf.readUint16() == 0xBEEF);
  test_assert(bf.readInt16() == -12345);
  test_assert(bf.readUint32() == 0xDEADBEEFu);
  test_assert(bf.readInt32() == -987654321);
  test_assert(bf.readUint64() == 0x0123456789ABCDEFull);
  test_assert(bf.readInt64() == -1234567890123456789ll);
  test_assert(bf.readFloat() == 3.5f);
  test_assert(bf.readDouble() == -2.71828);
}

static void test_string_roundtrip()
{
  std::stringstream ss;
  BinFile bf(ss);

  util::string small = "hello, world";
  util::string empty = "";
  util::string large;
  for (int i = 0; i < 1024; i++) {
    large += char('a' + (i % 26));
  }

  bf.writeString(small);
  bf.writeString(empty);
  bf.writeString(large);

  util::string s1 = bf.readString();
  util::string s2 = bf.readString();
  util::string s3 = bf.readString();

  test_assert(s1 == small);
  test_assert(s2 == empty);
  test_assert(s3 == large);
}

static void test_header_roundtrip()
{
  std::stringstream ss;
  BinFile bf(ss);
  bf.version[0] = 4;
  bf.version[1] = 2;
  bf.version[2] = 9;
  bf.writeHeader();

  std::stringstream ss2(ss.str());
  BinFile bf2(ss2);
  test_assert(bf2.readHeader());
  test_assert(bf2.version[0] == 4);
  test_assert(bf2.version[1] == 2);
  test_assert(bf2.version[2] == 9);
  test_assert(bf2.littleEndian == bf.littleEndian);
}

static void test_static_array()
{
  IOStructDef *def = StaticArrayHolder::defineSchema();

  StaticArrayHolder a;
  a.fixed[0] = 10;
  a.fixed[1] = -20;
  a.fixed[2] = 30;
  a.fixed[3] = -40;

  std::stringstream ss;
  BinFile bf(ss);
  bf.writeStructBySchema(&a, def);

  // Static arrays write only data — 4 ints = 16 bytes.
  test_assert(ss.str().size() == 16);

  StaticArrayHolder b{};
  bf.readStructBySchema(&b, def);
  test_assert(b.fixed[0] == 10);
  test_assert(b.fixed[1] == -20);
  test_assert(b.fixed[2] == 30);
  test_assert(b.fixed[3] == -40);

  deleteSchema(def);
}

static void test_nested_struct()
{
  IOStructDef *def = Outer::defineSchema();

  Outer src;
  src.tag = 0x1234;
  src.inner.x = 99;
  src.inner.y = 1.5f;

  std::stringstream ss;
  BinFile bf(ss);
  bf.writeStructBySchema(&src, def);

  Outer dst{};
  bf.readStructBySchema(&dst, def);
  test_assert(dst.tag == 0x1234);
  test_assert(dst.inner.x == 99);
  test_assert(dst.inner.y == 1.5f);

  deleteSchema(def);
}

static void test_pointer_field()
{
  IOStructDef *def = PointerHolder::defineSchema();

  int32_t target = 42;
  PointerHolder src;
  src.raw = &target;

  std::stringstream ss;
  BinFile bf(ss);
  bf.writeStructBySchema(&src, def);

  PointerHolder dst{};
  bf.readStructBySchema(&dst, def);
  test_assert(dst.raw == &target);
  test_assert(*dst.raw == 42);

  PointerHolder nullSrc;
  nullSrc.raw = nullptr;
  std::stringstream ss2;
  BinFile bf2(ss2);
  bf2.writeStructBySchema(&nullSrc, def);
  PointerHolder nullDst;
  nullDst.raw = reinterpret_cast<int32_t *>(uintptr_t(0xdead));
  bf2.readStructBySchema(&nullDst, def);
  test_assert(nullDst.raw == nullptr);

  deleteSchema(def);
}

static void test_string_field_via_schema()
{
  IOStructDef *def = WithString::defineSchema();

  WithString src;
  src.s = "binfile schema test";

  std::stringstream ss;
  BinFile bf(ss);
  bf.writeStructBySchema(&src, def);

  WithString dst;
  bf.readStructBySchema(&dst, def);
  test_assert(dst.s == src.s);

  deleteSchema(def);
}

static void test_all_numerics_via_schema()
{
  IOStructDef *def = AllNumerics::defineSchema();

  AllNumerics src;
  src.u8 = 0x12;
  src.i8 = -3;
  src.u16 = 0xABCD;
  src.i16 = -32000;
  src.u32 = 0xCAFEBABEu;
  src.i32 = -100000;
  src.u64 = 0xFEEDFACECAFEBABEull;
  src.i64 = -9000000000ll;
  src.f32 = 1.25f;
  src.f64 = -3.14159;

  std::stringstream ss;
  BinFile bf(ss);
  bf.writeStructBySchema(&src, def);

  AllNumerics dst{};
  bf.readStructBySchema(&dst, def);
  test_assert(dst.u8 == src.u8);
  test_assert(dst.i8 == src.i8);
  test_assert(dst.u16 == src.u16);
  test_assert(dst.i16 == src.i16);
  test_assert(dst.u32 == src.u32);
  test_assert(dst.i32 == src.i32);
  test_assert(dst.u64 == src.u64);
  test_assert(dst.i64 == src.i64);
  test_assert(dst.f32 == src.f32);
  test_assert(dst.f64 == src.f64);

  deleteSchema(def);
}

static void test_schema_roundtrip()
{
  IOStructDef *innerDef = Inner::defineSchema();
  IOStructDef *outerDef = Outer::defineSchema();
  IOStructDef *strDef = WithString::defineSchema();

  StructSchema src;
  src.addStruct(innerDef);
  src.addStruct(outerDef);
  src.addStruct(strDef);

  std::stringstream ss;
  BinFile bf(ss);
  bf.writeSchema(src);

  StructSchema dst;
  bf.readSchema(dst);

  test_assert(dst.structs.size() == 3);
  test_assert(dst.findByName("Inner") != nullptr);
  test_assert(dst.findByName("Outer") != nullptr);
  test_assert(dst.findByName("WithString") != nullptr);

  IOStructDef *outerCopy = dst.findByName("Outer");
  test_assert(outerCopy->fields.size() == 2);
  test_assert(outerCopy->fields[0]->type == FieldTypes::INT32);
  test_assert(util::string(outerCopy->fields[0]->name) == util::string("tag"));
  test_assert(outerCopy->fields[1]->type == FieldTypes::STRUCT);
  test_assert(util::string(outerCopy->fields[1]->name) == util::string("inner"));

  IOStructDef *innerCopy = static_cast<IOStructDef *>(outerCopy->fields[1]);
  test_assert(util::string(innerCopy->structName) == util::string("Inner"));
  test_assert(innerCopy->fields.size() == 2);
  test_assert(innerCopy->fields[0]->type == FieldTypes::INT32);
  test_assert(innerCopy->fields[1]->type == FieldTypes::FLOAT);

  IOStructDef *strCopy = dst.findByName("WithString");
  test_assert(strCopy->fields.size() == 1);
  test_assert(strCopy->fields[0]->type == FieldTypes::STRING);

  // Cleanup — both schemas own distinct allocations.
  for (IOStructDef *s : src.structs) {
    deleteSchema(s);
  }
  for (IOStructDef *s : dst.structs) {
    deleteSchema(s);
  }
}

static void test_full_file()
{
  IOStructDef *outerDef = Outer::defineSchema();

  StructSchema schema;
  schema.addStruct(outerDef);

  Outer src;
  src.tag = 7;
  src.inner.x = 11;
  src.inner.y = 2.5f;

  std::stringstream ss;
  {
    BinFile bf(ss);
    bf.writeFile(schema, [&](BinFile &out) {
      out.writeUint32(1); // numTopLevel
      out.writeStructBySchema(&src, outerDef);
    });
  }

  std::stringstream ss2(ss.str());
  ss2.seekg(0);
  Outer dst{};
  StructSchema schemaIn;
  {
    BinFile bf(ss2);
    bool ok = bf.readFile(schemaIn, [&](BinFile &in) {
      uint32_t n = in.readUint32();
      test_assert(n == 1);
      in.readStructBySchema(&dst, outerDef);
    });
    test_assert(ok);
  }

  test_assert(dst.tag == 7);
  test_assert(dst.inner.x == 11);
  test_assert(dst.inner.y == 2.5f);
  test_assert(schemaIn.structs.size() == 1);
  test_assert(schemaIn.findByName("Outer") != nullptr);

  // Cleanup
  deleteSchema(outerDef);
  for (IOStructDef *s : schemaIn.structs) {
    deleteSchema(s);
  }
}

static void test_bound_io_constructor()
{
  const auto *st = BoundIO::defineBindings();
  test_assert(st->findConstructor("io") != nullptr);
  test_assert(st->findConstructor("nope") == nullptr);

  IOStructDef *iodef = BoundIO::defineSchema();

  BoundIO src;
  src.a = 123;
  src.b = -456;

  std::stringstream ss;
  BinFile bf(ss);
  bf.writeStructBySchema(&src, iodef);

  alignas(BoundIO) char buf[sizeof(BoundIO)];
  bf.readStruct(reinterpret_cast<BoundIO *>(buf), iodef, st);

  BoundIO *out = reinterpret_cast<BoundIO *>(buf);
  test_assert(out->a == 123);
  test_assert(out->b == -456);
  out->~BoundIO();

  deleteSchema(iodef);
}

static void test_struct_data_basic()
{
  // Numerics
  IOStructDef *numDef = AllNumerics::defineSchema();
  AllNumerics src;
  src.u8 = 0x12;
  src.i8 = -3;
  src.u16 = 0xABCD;
  src.i16 = -32000;
  src.u32 = 0xCAFEBABEu;
  src.i32 = -100000;
  src.u64 = 0xFEEDFACECAFEBABEull;
  src.i64 = -9000000000ll;
  src.f32 = 1.25f;
  src.f64 = -3.14159;

  std::stringstream ss;
  BinFile bf(ss);
  bf.writeStructBySchema(&src, numDef);

  StructData *sd = bf.readStructData(numDef);
  test_assert(sd->getUint8("u8") == src.u8);
  test_assert(sd->getInt8("i8") == src.i8);
  test_assert(sd->getUint16("u16") == src.u16);
  test_assert(sd->getInt16("i16") == src.i16);
  test_assert(sd->getUint32("u32") == src.u32);
  test_assert(sd->getInt32("i32") == src.i32);
  test_assert(sd->getUint64("u64") == src.u64);
  test_assert(sd->getInt64("i64") == src.i64);
  test_assert(sd->getFloat("f32") == src.f32);
  test_assert(sd->getDouble("f64") == src.f64);
  test_assert(sd->get("missing") == nullptr);
  delete sd;
  deleteSchema(numDef);

  // String
  IOStructDef *strDef = WithString::defineSchema();
  WithString s;
  s.s = "hello via StructData";
  std::stringstream ss2;
  BinFile bf2(ss2);
  bf2.writeStructBySchema(&s, strDef);
  StructData *sd2 = bf2.readStructData(strDef);
  test_assert(sd2->getString("s") == s.s);
  delete sd2;
  deleteSchema(strDef);
}

static void test_struct_data_nested()
{
  IOStructDef *def = Outer::defineSchema();

  Outer src;
  src.tag = 0x77;
  src.inner.x = 42;
  src.inner.y = 6.5f;

  std::stringstream ss;
  BinFile bf(ss);
  bf.writeStructBySchema(&src, def);

  StructData *sd = bf.readStructData(def);
  test_assert(sd->getInt32("tag") == 0x77);
  StructData *inner = sd->getStruct("inner");
  test_assert(inner != nullptr);
  test_assert(inner->getInt32("x") == 42);
  test_assert(inner->getFloat("y") == 6.5f);
  test_assert(sd->getStruct("tag") == nullptr);  // wrong type → nullptr
  delete sd;
  deleteSchema(def);
}

static void test_struct_data_static_array()
{
  IOStructDef *def = StaticArrayHolder::defineSchema();

  StaticArrayHolder src;
  src.fixed[0] = 100;
  src.fixed[1] = -200;
  src.fixed[2] = 300;
  src.fixed[3] = -400;

  std::stringstream ss;
  BinFile bf(ss);
  bf.writeStructBySchema(&src, def);

  StructData *sd = bf.readStructData(def);
  ArrayData *arr = sd->getArray("fixed");
  test_assert(arr != nullptr);
  test_assert(arr->elements.size() == 4);
  test_assert(arr->elements[0]->getInt32() == 100);
  test_assert(arr->elements[1]->getInt32() == -200);
  test_assert(arr->elements[2]->getInt32() == 300);
  test_assert(arr->elements[3]->getInt32() == -400);
  delete sd;
  deleteSchema(def);
}

int main()
{
  test_numeric_roundtrip(false);
  test_numeric_roundtrip(true);
  test_string_roundtrip();
  test_header_roundtrip();
  test_static_array();
  test_nested_struct();
  test_pointer_field();
  test_string_field_via_schema();
  test_all_numerics_via_schema();
  test_schema_roundtrip();
  test_full_file();
  test_struct_data_basic();
  test_struct_data_nested();
  test_struct_data_static_array();
  test_bound_io_constructor();

  return test_end();
}
