#include "../../attr/schema.h"
using namespace litestl::attr;
using A = AttrDef<"pos", Column<litestl::math::double3>>;
using B = AttrDef<"pos", Column<float>>;
int main()
{
  TypedGroup<Schema<A, B>> g; // two attributes named "pos"
  return 0;
}
