/* The fixture as a WASM module: the WASM half of the conformance tests. */

#include "fixture.h"

extern "C" litestl::binding::BindingManager *LSTL_Fixture_GetManager()
{
  return fixture::fixtureManager();
}
