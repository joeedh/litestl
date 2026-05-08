#pragma once

#include "binding_base.h"

namespace litestl::binding::types {

struct ParentTemplateParam : public BindingBase {
  string templParamName;
  /** how far up the parent template is in the type hierarchy */
  int parentDepth;
  const BindingBase *concreteType;

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