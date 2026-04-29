#include "binding.h"
#include "binding/binding_base.h"
#include "binding_literal.h"
#include "binding_struct.h"
#include "binding_types.h"
#include "litestl/util/string.h"
#include "manager.h"

extern "C" {
struct [[gnu::packed]] WasmOffsets {
  struct {
    int name;
    int type;
  } Base;
  struct {
    int members;
    int methods;
    int constructors;
    int templateParams;
    int structSize;
  } Struct;
  struct {
    int ownerType;
    int params;
  } Constructor;
  struct {
    int name;
    int type;
  } ConstructorParam;
  struct {
    int name;
    int offset;
    int type;
  } StructMember;
  struct {
    int subtype;
    int flags;
  } Number;
  struct {
    int arrayType;
    int arraySize;
  } Array;
  struct {
    int litType;
    int litBind;
  } Literal;
  struct {
    int data;
  } NumLit;
  struct {
    int data;
  } BoolLit;
  struct {
    int data;
  } StrLit;
  struct {
    int ptrType;
    int isNonNull;
  } Pointer;
  struct {
    int refType;
  } Reference;
  struct {
    int name;
    int value;
  } EnumItem;
  struct {
    int items;
    int baseSize;
    int isBitMask;
  } Enum;
  struct {
    int name;
    int type;
  } TemplateParam;
  struct {
    int returnType;
    int params;
    int isConst;
    int isStatic;
  } Method;
  struct {
    int name;
    int type;
  } MethodParam;
  struct {
    int structs;
    int disPropName;
    int disPropType;
  } Union;
  struct UnionPair {
    int name;
    int type;
    int typeValue;
  } UnionPair;
  struct {
    int templParamName;
    int parentDepth;
  } ParentTemplateParam;
};

struct [[gnu::packed]] TypeSizes {
  struct {
    int StructMember;
    int StructBase;
    int TemplateParam;
    int StructMethod;
    int StructConstructor;
  } Struct;
  struct {
    int Boolean;
    int NumLit;
    int BoolLit;
    int StrLit;
    int Pointer;
    int Reference;
  } Types;
  struct {
    int ConstructorParam;
  } Constructor;
  struct {
    int EnumItem;
    int Enum;
  } Enum;
  struct {
    int Method;
    int MethodParam;
  } Method;
  struct {
    int Union;
    int UnionPair;
    int typeValue;
  } Union;
  struct {
    int ParentTemplateParam;
  } ParentTemplateParam;
};

struct BindingInfo {
  WasmOffsets offsets;
  TypeSizes sizes;
};

BindingInfo *LSTL_GetBindingInfo()
{
  BindingInfo *info = litestl::alloc::New<BindingInfo>("binding info");
  using namespace litestl::binding::types;
  using namespace litestl::binding;

  info->offsets.Base.name = offsetof(BindingBase, name);
  info->offsets.Base.type = offsetof(BindingBase, type);

  info->offsets.Struct.members = offsetof(_StructBase, members);
  info->offsets.Struct.methods = offsetof(_StructBase, methods);
  info->offsets.Struct.constructors = offsetof(_StructBase, constructors);
  info->offsets.Struct.templateParams = offsetof(_StructBase, templateParams);
  info->offsets.Struct.structSize = offsetof(_StructBase, structSize);

  info->offsets.Constructor.ownerType = offsetof(Constructor, ownerType);
  info->offsets.Constructor.params = offsetof(Constructor, params);

  info->offsets.ConstructorParam.name = offsetof(ConstructorParam, name);
  info->offsets.ConstructorParam.type = offsetof(ConstructorParam, type);

  info->offsets.StructMember.name = offsetof(StructMember, name);
  info->offsets.StructMember.offset = offsetof(StructMember, offset);
  info->offsets.StructMember.type = offsetof(StructMember, type);

  using NumberI32 = Number<int32_t>;
  info->offsets.Number.subtype = offsetof(NumberI32, subtype);
  info->offsets.Number.flags = offsetof(NumberI32, flags);

  using ArrayBB = Array<BindingBase>;
  info->offsets.Array.arrayType = offsetof(ArrayBB, arrayType);
  info->offsets.Array.arraySize = offsetof(ArrayBB, arraySize);

  info->offsets.Literal.litType = offsetof(LiteralType, litType);
  info->offsets.Literal.litBind = offsetof(LiteralType, litBind);

  info->offsets.NumLit.data = offsetof(NumLitType, data);
  info->offsets.BoolLit.data = offsetof(BoolLitType, data);
  info->offsets.StrLit.data = offsetof(StrLitType, data);

  info->offsets.Pointer.ptrType = offsetof(Pointer, ptrType);
  info->offsets.Pointer.isNonNull = offsetof(Pointer, isNonNull);
  info->offsets.Reference.refType = offsetof(Reference, refType);

  info->offsets.EnumItem.name = offsetof(EnumItem, name);
  info->offsets.EnumItem.value = offsetof(EnumItem, value);
  info->offsets.Enum.items = offsetof(Enum, items);
  info->offsets.Enum.baseSize = offsetof(Enum, baseSize);
  info->offsets.Enum.isBitMask = offsetof(Enum, isBitMask);

  info->offsets.TemplateParam.name = offsetof(StructTemplate, name);
  info->offsets.TemplateParam.type = offsetof(StructTemplate, type);

  info->offsets.Method.returnType = offsetof(Method, returnType);
  info->offsets.Method.params = offsetof(Method, params);
  info->offsets.Method.isConst = offsetof(Method, isConst);
  info->offsets.Method.isStatic = offsetof(Method, isStatic);

  info->offsets.MethodParam.name = offsetof(MethodParam, name);
  info->offsets.MethodParam.type = offsetof(MethodParam, type);

  info->offsets.Union.structs = offsetof(Union<int>, structs);
  info->offsets.Union.disPropName = offsetof(Union<int>, disPropName);
  info->offsets.Union.disPropType = offsetof(Union<int>, disPropType);
  info->offsets.UnionPair.name = offsetof(UnionPair, name);
  info->offsets.UnionPair.type = offsetof(UnionPair, type);
  info->offsets.UnionPair.typeValue = offsetof(UnionPair, typeValue);

  info->offsets.ParentTemplateParam.templParamName =
      offsetof(ParentTemplateParam, templParamName);
  info->offsets.ParentTemplateParam.parentDepth =
      offsetof(ParentTemplateParam, parentDepth);

  info->sizes.Struct.StructMember = sizeof(StructMember);
  info->sizes.Struct.StructBase = sizeof(_StructBase);
  info->sizes.Struct.TemplateParam = sizeof(StructTemplate);
  info->sizes.Struct.StructMethod = sizeof(Method);
  info->sizes.Struct.StructConstructor = sizeof(Constructor);

  info->sizes.Types.Boolean = sizeof(Boolean);
  info->sizes.Types.NumLit = sizeof(NumLitType);
  info->sizes.Types.BoolLit = sizeof(BoolLitType);
  info->sizes.Types.StrLit = sizeof(StrLitType);
  info->sizes.Types.Pointer = sizeof(Pointer);
  info->sizes.Types.Reference = sizeof(Reference);

  info->sizes.Constructor.ConstructorParam = sizeof(ConstructorParam);
  info->sizes.Enum.EnumItem = sizeof(EnumItem);
  info->sizes.Enum.Enum = sizeof(Enum);

  info->sizes.Method.MethodParam = sizeof(MethodParam);
  info->sizes.Method.Method = sizeof(Method);

  info->sizes.Union.Union = sizeof(Union<int>);
  info->sizes.Union.UnionPair = sizeof(UnionPair);
  info->sizes.Union.typeValue = sizeof(UnionPair::typeValue);

  info->sizes.ParentTemplateParam.ParentTemplateParam = sizeof(ParentTemplateParam);

  return info;
}
void LSTL_FreeBindingInfo(BindingInfo *info)
{
  litestl::alloc::Delete(info);
}
}

using namespace litestl::binding;
using namespace litestl;
extern "C" {
const char *LSTL_Binding_GetKeys(BindingManager *m)
{
  string s = "";
  for (auto &key : m->getBindings().keys()) {
    s += key + "\\";
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
const BindingBase *LSTL_Binding_Get(BindingManager *m, const char *key)
{
  if (m->getBindings().contains(key)) {
    return m->getBindings().lookup(key);
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
const BindingBase *
LSTL_Method_GetParam(const types::Method *m, size_t i, const char **outName)
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

size_t LSTL_Struct_GetConstructorCount(const types::_StructBase *s)
{
  return s->constructors.size();
}
const types::Constructor *LSTL_Struct_GetConstructor(const types::_StructBase *s,
                                                     size_t i)
{
  if (i >= s->constructors.size()) {
    return nullptr;
  }
  return s->constructors[static_cast<int>(i)];
}
const char *LSTL_Constructor_GetName(const types::Constructor *c)
{
  return c->name.c_str();
}
const BindingBase *LSTL_Constructor_GetOwner(const types::Constructor *c)
{
  return c->ownerType;
}
size_t LSTL_Constructor_GetParamCount(const types::Constructor *c)
{
  return c->params.size();
}
const BindingBase *
LSTL_Constructor_GetParam(const types::Constructor *c, size_t i, const char **outName)
{
  if (i >= c->params.size()) {
    return nullptr;
  }
  const auto &p = c->params[static_cast<int>(i)];
  if (outName) {
    *outName = p.name.c_str();
  }
  return p.type;
}
void LSTL_Constructor_Invoke(const types::Constructor *c, void *outBuf, void **args)
{
  if (c->thunk) {
    c->thunk(outBuf, args);
  }
}
void LSTL_Destructor_Invoke(const types::_StructBase *s, void *obj)
{
  if (s->destructorThunk) {
    (*s->destructorThunk)(obj);
  }
}

int LSTL_GetBindTypeSize(BindingBase *b)
{
  return b->getSize();
}

unsigned char *LSTL_GenerateTypescript(BindingManager *manager, int *size_out)
{
  util::Vector<const BindingBase *> types;
  for (auto *type : manager->getBindings().values()) {
    types.append(type);
  }
  int count = 0;
  auto files = generators::generateTypescript(types);

  for (auto &key : files->keys()) {
    auto &value = files->lookup(key);
    count += key.size() + 4 + value.size() + 4;
  }

  unsigned char *s = static_cast<unsigned char *>(malloc(count));
  unsigned char *result = s;

  for (auto &key : files->keys()) {
    auto &value = files->lookup(key);

    int n = key.size();
    unsigned char *c = reinterpret_cast<unsigned char *>(&n);

    // write size integer of filename
    for (int i = 0; i < 4; i++) {
      *s++ = c[i];
    }

    // write filename
    for (int i = 0; i < key.size(); i++) {
      *s++ = key[i];
    }

    n = value.size();
    // write size integer of file buffer
    for (int i = 0; i < 4; i++) {
      *s++ = c[i];
    }

    // write file buffer
    for (int i = 0; i < value.size(); i++) {
      *s++ = value[i];
    }
  }

  *size_out = count;
  return result;
}

void LSTL_FreeTypescriptString(char *s)
{
  free(s);
}

/** Note: must free result with memRelease() */
const char *LSTL_Binding_GetFullName(const BindingBase *type)
{
  string s = type->buildFullName();
  char *s2 = static_cast<char *>(alloc::alloc("LSTL_Binding_GetFullName", s.size() + 1));

  memcpy(s2, s.c_str(), s.size());
  // explicitly null-terminate
  s2[s.size()] = 0;
  return s2;
}

void LSTL_PrintAllocBlocks(bool includePermanent)
{
  alloc::print_blocks(includePermanent);
}
void *memAlloc(const char *tag, size_t size)
{
  return alloc::alloc(tag, size);
}

void memRelease(void *mem)
{
  alloc::release(mem);
}
void printMemBlocks()
{
  alloc::print_blocks(true);
}
void *_rawAlloc(int size)
{
  return malloc(size);
}
void _rawRelease(void *ptr)
{
  free(ptr);
}
}
