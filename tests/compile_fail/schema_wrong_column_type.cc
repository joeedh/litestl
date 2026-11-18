#include "litestl/attr/schema.h"
using namespace litestl::attr;
using Pos = AttrDef<"pos", Column<litestl::math::double3>>;
int main()
{
  TypedGroup<Schema<Pos>> g;
  Column<float> &c = g.get<"pos">(); // "pos" holds double3, not float
  (void)c;
  return 0;
}
