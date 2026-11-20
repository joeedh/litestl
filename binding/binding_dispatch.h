#pragma once

/*
 * How a JS call picks among the instantiations of a function template. Shared
 * by the TS generator and the N-API runtime; the WASM runtime has a TS copy in
 * typescriptRuntime/dispatch.ts that must agree with it.
 *
 * - Single: one candidate, nothing to choose.
 * - Infer:  every candidate has a different list of bound-struct parameters,
 *           so the bound objects passed in identify the instantiation. Value
 *           structs (strings, spans, vectors) arrive as plain JS values and
 *           don't count.
 * - Select: the caller passes a selector key as the first argument: the full
 *           names of the template arguments joined with ',' ("float",
 *           "float,int32").
 */

#include "binding.h"

#include <cstring>

namespace litestl::binding::dispatch {

enum class Mode { Single, Infer, Select };

/** Structs JS passes as plain values (strings, arrays) rather than as bound
 * objects, so a call's arguments can't identify their instantiation. */
inline bool isValueStruct(const types::_StructBase *st)
{
  for (const char *name : {"litestl::util::String", "litestl::util::CString",
                           "litestl::util::Span", "litestl::util::Vector"})
  {
    if (strcmp(st->name.c_str(), name) == 0) {
      return true;
    }
  }
  return false;
}

/** The bound struct a parameter carries, by value, pointer or reference. */
inline const types::_StructBase *paramStruct(const BindingBase *t)
{
  if (t->type == BindingType::Pointer) {
    t = static_cast<const types::Pointer *>(t)->ptrType;
  } else if (t->type == BindingType::Reference) {
    t = static_cast<const types::Reference *>(t)->refType;
  }
  if (t->type != BindingType::Struct) {
    return nullptr;
  }
  const auto *st = static_cast<const types::_StructBase *>(t);
  return isValueStruct(st) ? nullptr : st;
}

/** Selector key for a candidate: the owner struct's template arguments (for
 * static methods of a template struct), then the method's own. */
inline string selectorKey(const types::Method *m, const types::_StructBase *owner = nullptr)
{
  string key;
  bool first = true;
  auto addParams = [&](const util::Vector<types::StructTemplate> &params) {
    for (const auto &p : params) {
      if (!first) {
        key += ",";
      }
      first = false;
      key += p.type->buildFullName();
    }
  };
  if (owner) {
    addParams(owner->templateParams);
  }
  addParams(m->templateParams);
  return key;
}

inline string structSignature(const types::Method *m)
{
  string sig;
  bool any = false;
  for (const auto &p : m->params) {
    const types::_StructBase *st = paramStruct(p.type);
    sig += st ? st->buildFullName() : string("-");
    sig += "|";
    any = any || st;
  }
  return any ? sig : string("");
}

inline Mode mode(const util::Vector<const types::Method *> &group)
{
  if (group.size() <= 1) {
    return Mode::Single;
  }
  util::Vector<string> sigs;
  for (const types::Method *m : group) {
    string sig = structSignature(m);
    if (sig.size() == 0 || sigs.contains(sig)) {
      return Mode::Select;
    }
    sigs.append(sig);
  }
  return Mode::Infer;
}

} // namespace litestl::binding::dispatch
