#include "litestl/attr/bool_column.h"
#include "litestl/attr/schema.h"

#include "test_harness.h"

#include <vector>

test_init;

using namespace litestl;
using namespace litestl::attr;

using Pos = AttrDef<"pos", Column<math::double3>>;
struct VertEdge : AttrDef<".v.e", Column<uint32_t>, ColumnFlags::Topo> {
  static uint32_t default_value()
  {
    return 0xffffffffu;
  }
};
using Select = AttrDef<"select", BoolColumn>;
using VertexSchema = Schema<Pos, VertEdge, Select>;

using Uv = AttrDef<"uv", Column<math::float2>>; /* optional, not a builtin */
struct Temperature : AttrDef<"temperature", Column<float>> {
  static float default_value()
  {
    return 20.0f;
  }
};

static_assert(VertexSchema::size == 3);
static_assert(VertexSchema::index_of("select") == 2 && VertexSchema::index_of("nope") == VertexSchema::npos);
static_assert(TypedGroup<VertexSchema>::slot<".v.e">() == 1);
static_assert(std::is_same_v<decltype(std::declval<TypedGroup<VertexSchema> &>().get<"pos">()), Column<math::double3> &>);
static_assert(AttrDefinition<Pos> && !AttrDefinition<int>);

using ExtendedSchema = VertexSchema::extend<Temperature>;
static_assert(ExtendedSchema::size == 4 && ExtendedSchema::index_of("temperature") == 3);

static void run()
{
  TypedGroup<VertexSchema> verts(4);
  verts.resize(10);
  test_assert(verts.group().builtin_count() == 3);
  test_assert(verts.get<".v.e">()[3] == 0xffffffffu);   /* custom default */
  test_assert(has_flag(verts.get<VertEdge>().flags(), ColumnFlags::Topo));
  verts.get<"pos">().set(2, math::double3(1, 2, 3));
  set_bool(verts.get<"select">(), 2, true);
  test_assert(verts.get<Pos>()[2][1] == 2.0 && get_bool(verts.get<Select>(), 2));

  /* copies keep the schema accessors valid (slots, not pointers) */
  TypedGroup<VertexSchema> copy = verts;
  verts.get<"pos">().set(2, math::double3(9, 9, 9));
  test_assert(copy.get<"pos">()[2][0] == 1.0);

  /* optional attributes by definition */
  Column<math::float2> *uv = ensure<Uv>(verts.group());
  test_assert(uv && find<Uv>(verts.group()) == uv && uv->size() == 10);
  Column<float> *t = ensure<Temperature>(verts.group());
  test_assert(t && (*t)[0] == 20.0f);
  test_assert(!find<Temperature>(copy.group()));

  /* the schema layout is what read() verifies */
  io::ByteWriter w;
  verts.group().write(w);
  TypedGroup<VertexSchema> back(4);
  io::ByteReader r(w.data(), w.size());
  const char *err = nullptr;
  test_assert(back.group().read(r, {}, &err));
  test_assert(back.group().content_hash() == verts.group().content_hash());
  test_assert(back.get<"pos">()[2][0] == 9.0 && find<Uv>(back.group()));

  /* a group with another schema rejects the stream */
  TypedGroup<ExtendedSchema> other(4);
  io::ByteReader r2(w.data(), w.size());
  test_assert(!other.group().read(r2, {}, &err));

  /* extended schema */
  other.resize(5);
  test_assert(other.get<"temperature">()[4] == 20.0f && other.get<"pos">().size() == 5);
}

/* Schema migration: an old layout's columns move into a new layout. */
struct OldRef : AttrDef<".v.old", Column<uint32_t>, ColumnFlags::Topo> {};
struct NewRef : AttrDef<".v.new", Column<math::double2>, ColumnFlags::Topo> {};
using OldSchema = Schema<Pos, OldRef, VertEdge, Select>;
using NewSchema = Schema<Pos, VertEdge, NewRef, Select>;
struct WrongEdge : AttrDef<".v.e", Column<float>, ColumnFlags::Topo> {};
using WrongSchema = Schema<Pos, WrongEdge, Select>;

static void run_migrate()
{
  TypedGroup<OldSchema> old(4);
  old.resize(7);
  old.get<"pos">().set(3, math::double3(1, 2, 3));
  old.get<".v.e">().set(5, 42);
  old.get<".v.old">().set(1, 9);
  set_bool(old.get<"select">(), 6, true);
  ensure<Uv>(old.group())->set(2, math::float2(0.5f, 0.25f));
  auto keep = [](std::string_view n, const ColumnBase &) { return n != ".v.old"; };
  TypedGroup<NewSchema> nw(4);
  const char *err = nullptr;
  test_assert(nw.group().migrate_from(old.group(), keep, &err));
  test_assert(nw.size() == 7 && nw.get<"pos">()[3][2] == 3.0 && nw.get<".v.e">()[5] == 42);
  test_assert(nw.get<".v.e">()[0] == 0xffffffffu && get_bool(nw.get<"select">(), 6));
  test_assert(nw.get<".v.new">()[4][0] == 0.0 && nw.get<".v.new">().size() == 7);
  test_assert(find<Uv>(nw.group()) && (*find<Uv>(nw.group()))[2][1] == 0.25f && !nw.group().find_any(".v.old"));
  /* shared copy-on-write: writing one side leaves the other */
  nw.get<".v.e">().set(5, 7);
  test_assert(old.get<".v.e">()[5] == 42);
  /* an old builtin with no counterpart, kept: refused */
  auto all = [](std::string_view, const ColumnBase &) { return true; };
  TypedGroup<NewSchema> nw2(4);
  test_assert(!nw2.group().migrate_from(old.group(), all, &err) && nw2.size() == 0);
  /* a type mismatch: refused, nothing changed */
  TypedGroup<WrongSchema> wr(4);
  test_assert(!wr.group().migrate_from(old.group(), keep, &err) && wr.size() == 0);
}

int main()
{
  run();
  run_migrate();
  return test_end();
}
