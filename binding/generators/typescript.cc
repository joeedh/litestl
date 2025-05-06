#include "typescript.h"
#include "../../path/path.h"
#include "../binding.h"
#include <regex>
#include <string>

namespace litestl::binding::generators {
using util::string;
using util::Vector;

static void recurse(const BindingBase *type,
                    util::Map<string, const BindingBase *> &typeMap)
{
  if (typeMap.contains(type->name)) {
    return;
  }

  if (type->type == BindingType::Struct) {
    const types::Struct<void> *st = static_cast<const types::Struct<void> *>(type);
    typeMap.add(type->name, type);

    for (auto &member : st->members) {
      recurse(member.type, typeMap);
    }
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

  auto getFileName = [](const string &name) {
    std::string filename = name.c_str();
    return string((std::regex_replace(filename, std::regex("::"), "/") + ".ts").c_str());
  };

  auto formatType = [](const BindingBase *type) {
    std::string filename = type->name.c_str();
    int i = filename.find_last_of(':');
    if (i >= 0) {
      return string(filename.substr(i + 1, filename.size() - i - 1).c_str());
    }
    return string((std::regex_replace(filename, std::regex("::"), ".")).c_str());
  };
  auto formatImport = [&getFileName, formatType](const BindingBase *type, string filename) {
    string s;
    s += "import {" + formatType(type) + "} from \"" + path::relative(filename, getFileName(type->name)) + "\";";
    return s;
  };

  for (auto type : typeMap.values()) {
    if (type->type != BindingType::Struct) {
      continue;
    }
    const types::Struct<void> *st = static_cast<const types::Struct<void> *>(type);

    string s = "";
    string filename = getFileName(type->name);

    Vector<string> imports;

    s += "export interface " + formatType(type) + " {\n";
    for (auto &member : st->members) {
      s += "  " + member.name + ": " + formatType(member.type) + "\n";
      if (member.type->type == BindingType::Struct) {
        imports.append(formatImport(member.type, filename));
      }
    }
    s += "}\n";

    string importString = "";
    for (auto &import : imports) {
      importString += import + "\n";
    }
    s = importString + "\n" + s;

    files->add(filename, s);
  }
  return files;
}
} // namespace litestl::binding::generators
