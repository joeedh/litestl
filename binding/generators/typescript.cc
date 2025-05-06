#include "typescript.h"
#include "../binding.h"
#include <regex>
#include <string>

namespace litestl::binding::generators {
using util::string;
using util::Vector;

static void recurse(const BindingBase *type, util::Map<string, const BindingBase *> &typeMap)
{
  if (typeMap.contains(type->name)) {
    return;
  }

  if (type->type == BindingType::Struct) {
    const types::Struct<void> *st = static_cast<const types::Struct<void> *>(type);
    typeMap.add(type->name, type);
  }
}
util::Map<string, string> *generateTypescript(Vector<const BindingBase *> &types)
{
  util::Map<string, string> *files =
      alloc::New<util::Map<string, string>>("generateTypescript result");
  util::Map<string, const BindingBase *> typeMap;

  for (const auto type : types) {
    recurse(type, typeMap);
  }

  for (auto &type : typeMap.values()) {
    string s = "";
    std::string filename = type->name.c_str();
    filename = std::regex_replace(filename, std::regex("::"), "/") + ".js";

    files->add(filename.c_str(), s);
  }
  return files;
}
} // namespace litestl::binding::generators
