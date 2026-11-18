// Positive control: must compile (proves the include setup of the negative tests).
#include "litestl/attr/schema.h"
using namespace litestl::attr;
using Pos = AttrDef<"pos", Column<litestl::math::double3>>;
int main()
{
  TypedGroup<Schema<Pos>> g;
  g.get<"pos">();
  return 0;
}
