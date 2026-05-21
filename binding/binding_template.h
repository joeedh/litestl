#pragma once

#include "binding_base.h"

namespace litestl::binding::types {

struct Enum;

struct ParentTemplateParam : public BindingBase {
  string templParamName;
  /** how far up the parent template is in the type hierarchy */
  int parentDepth;
  const BindingBase *concreteType;
  /** Optional enum that discriminates the containing struct's instantiations
   * across some `Union`. When set, the TS generator looks up the matching
   * Union (the one whose `disPropType` is this enum), emits this field as
   * `<Union.mapName>[K]`, and adds `K extends keyof <map>` to the interface's
   * template parameter list. We store the *enum* rather than the Union so
   * that templated `defineBindings` doesn't depend on the Union global being
   * initialised before the struct's first `Bind<>` call. */
  const Enum *disambiguator = nullptr;

  /**
   * used to store the original name of a parent class's template parameter,
   * wraps the resolved (remember we never bind non-instantiated templates)
   * concrete type.
   */
  ParentTemplateParam(string templParamName,
                      int parentDepth,
                      const BindingBase *concreteType)
      : templParamName(templParamName), parentDepth(parentDepth),
        concreteType(concreteType),
        BindingBase(BindingType::ParentTemplParam, templParamName)
  {
  }
  ParentTemplateParam(string templParamName,
                      int parentDepth,
                      const BindingBase *concreteType,
                      const Enum *disambiguator)
      : templParamName(templParamName), parentDepth(parentDepth),
        concreteType(concreteType), disambiguator(disambiguator),
        BindingBase(BindingType::ParentTemplParam, templParamName)
  {
  }
  ParentTemplateParam(const ParentTemplateParam &b) = default;

  virtual BindingBase *clone() const override
  {
    return new ParentTemplateParam(*this);
  }

  virtual string buildFullName() const override
  {
    return templParamName;
  }

  virtual size_t getSize() const override
  {
    return 0;
  }
};
} // namespace litestl::binding::types