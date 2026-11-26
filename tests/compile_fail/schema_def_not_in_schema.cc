#include "../../attr/schema.h"
using namespace litestl::attr;
using Pos = AttrDef<"pos", Column<litestl::math::double3>>;
using Uv = AttrDef<"uv", Column<litestl::math::float2>>;
int main()
{
  TypedGroup<Schema<Pos>> g;
  g.get<Uv>(); // Uv is an optional attribute, not a builtin of this schema
  return 0;
}
