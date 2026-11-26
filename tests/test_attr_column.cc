#include "../attr/bool_column.h"
#include "../attr/column.h"
#include "../attr/registry.h"

#include "test_harness.h"

test_init;

using namespace litestl;
using namespace litestl::attr;

static void smoke()
{
  Column<float> c(1.5f, 2);
  c.resize(10);
  test_assert(c[3] == 1.5f && c.page_count() == 3);
  c.set(5, 2.0f);
  test_assert(c[5] == 2.0f && c.page_state(1) == PageState::Dense);
  auto snap = c.share();
  c.set(5, 3.0f);
  test_assert(static_cast<Column<float> &>(*snap)[5] == 2.0f && c[5] == 3.0f);
  test_assert(&c.type() == ColumnTypeRegistry::global().find("f32"));
  test_assert(ColumnTypeRegistry::global().find("f64x3") && ColumnTypeRegistry::global().find("bool"));
  BoolColumn b;
  b.resize(4);
  set_bool(b, 2, true);
  test_assert(get_bool(b, 2) && !get_bool(b, 1));
}

int main()
{
  smoke();
  return test_end();
}
