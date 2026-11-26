#include "../../attr/schema.h"
using namespace litestl::attr;
using Bad = AttrDef<"count", int>; // not a column type
int main()
{
  TypedGroup<Schema<Bad>> g;
  return 0;
}
