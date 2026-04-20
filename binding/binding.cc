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

size_t LSTL_Struct_GetMethodCount(const types::_StructBase *s)
{
  return s->methods.size();
}
const types::Method *LSTL_Struct_GetMethod(const types::_StructBase *s, size_t i)
{
  if (i >= s->methods.size()) {
    return nullptr;
  }
  return s->methods[static_cast<int>(i)];
}
size_t LSTL_Method_GetParamCount(const types::Method *m)
{
  return m->params.size();
}
const BindingBase *LSTL_Method_GetParam(const types::Method *m, size_t i,
                                        const char **outName)
{
  if (i >= m->params.size()) {
    return nullptr;
  }
  const auto &p = m->params[static_cast<int>(i)];
  if (outName) {
    *outName = p.name.c_str();
  }
  return p.type;
}
const BindingBase *LSTL_Method_GetReturn(const types::Method *m)
{
  return m->returnType;
}
const char *LSTL_Method_GetName(const types::Method *m)
{
  return m->name.c_str();
}
int LSTL_Method_IsConst(const types::Method *m)
{
  return m->isConst ? 1 : 0;
}
void LSTL_Method_Invoke(const types::Method *m, void *self, void **args, void *retBuf)
{
  if (m->thunk) {
    m->thunk(self, args, retBuf);
  }
}
}
