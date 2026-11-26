#include "../../attr/schema.h"
using namespace litestl::attr;
using Pos = AttrDef<"pos", Column<litestl::math::double3>>;
int main()
{
  TypedGroup<Schema<Pos>> g;
  g.get<"position">(); // typo: not in the schema
  return 0;
}
