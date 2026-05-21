#include "binding_base.h"
#include "binding_struct.h"

#include <functional>

namespace litestl::binding::types {
using litestl::util::Vector;

struct UnionPair {
  string name;
  const _StructBase *type;
  // we use a fixed size value to
  // prevent template instantiation from
  // producing different class layouts.
  char typeValue[8];
};

/** This is a TS-style type union with a disambiguation property, not an actual C-type
 * union */
struct Union : public BindingBase {
  Vector<UnionPair> structs;
  /** Fully-qualified name of the TS mapped-type emitted from this union
   * (e.g. "sculptcore::gpu::UniformBindTypeMap"). Also used as the binding's
   * own `name` so it has a stable filename in the generated TS output. */
  string mapName;
  // type disambiguation (key) property name
  string disPropName;
  const BindingBase *disPropType;
  std::function<int32_t(const void *)> disPropFunc;

  Union(string mapName, string disPropName, const BindingBase *disPropType)
      : BindingBase(BindingType::Union, mapName), mapName(mapName),
        disPropName(disPropName), disPropType(disPropType)
  {
  }
  Union(const Union &u) = default;
  Union &operator=(const Union &u) = default;
  
  template <typename T> void add(string name, T typeValue, const _StructBase *type)
  {
    UnionPair pair = {name, type, {}};
    T *ptr = reinterpret_cast<T *>(pair.typeValue);
    *ptr = typeValue;
    structs.append(pair);
  }

  size_t getSize() const override
  {
    // XXX what should this be?
    return 1;
  }

  string buildFullName() const override
  {
    Vector<string> names;
    for (auto &s : structs) {
      names.append(s.name);
    }
    return names.join("|");
  }

  virtual BindingBase *clone() const override
  {
    return new Union(*this);
  }
};
} // namespace litestl::binding::types
