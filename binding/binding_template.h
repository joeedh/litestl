#include "binding_base.h"

namespace litestl::binding::types {

struct ParentTemplateParam : public BindingBase {
  string templParamName;
  /** how far up the parent template is in the type hierarchy */
  int parentDepth;

  ParentTemplateParam(string templParamName, int parentDepth)
      : templParamName(templParamName), parentDepth(parentDepth),
        BindingBase(BindingType::ParentTemplParam, "parent template param")
  {
  }
  ParentTemplateParam(const ParentTemplateParam &b) = default;

  virtual BindingBase *clone() override
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