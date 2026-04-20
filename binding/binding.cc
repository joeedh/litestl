#include "binding.h"
#include "binding/binding_base.h"
#include "litestl/util/string.h"
#include "manager.h"


using namespace litestl::binding;
using namespace litestl;
extern "C" {
const char *LSTL_Binding_GetKeys(BindingManager *m)
{
  string s = "";
  for (auto &key : m->bindings.keys()) {
    s += key + "|";
  }

  char *r = static_cast<char *>(alloc::alloc("LSTL_Binding_GetKeys", s.size() + 1));
  memcpy(r, s.c_str(), s.size());
  r[s.size()] = 0;
  return r;
}
void LSTL_Binding_FreeKeys(const char *keys)
{
  alloc::release(const_cast<char *>(keys));
}
const BindingBase *LSTL_Binding_Get(BindingManager *m, const char *key) {
  if (m->bindings.contains(key)) {
    return m->bindings[key];
  }
  return nullptr;
}
}
