#include "typescript.h"
#include "../../path/path.h"
#include "../binding.h"
#include "util/function.h"
#include "util/set.h"
#include <regex>
#include <string>

namespace litestl::binding::generators {
using util::function_ref;
using util::Set;
using util::string;
using util::Vector;

struct TypescriptType {
  string name;
  Vector<TypescriptType *> typeParams;
};

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
    for (auto &param : st->templateParams) {
      recurse(param.type, typeMap);
    }
    for (const types::Method *m : st->methods) {
      if (m->returnType) {
        recurse(m->returnType, typeMap);
      }
      for (const auto &p : m->params) {
        recurse(p.type, typeMap);
      }
    }
    for (const types::Constructor *c : st->constructors) {
      for (const auto &p : c->params) {
        recurse(p.type, typeMap);
      }
    }
  }
}

util::Map<string, string> *generateTypescript(Vector<const BindingBase *> &types)
{
  using namespace litestl::binding::types;

  util::Map<string, string> *files =
      alloc::New<util::Map<string, string>>("generateTypescript result");
  util::Map<string, const BindingBase *> typeMap;

  for (const auto type : types) {
    recurse(type, typeMap);
  }

  auto getModuleName = [](const string &name) {
    std::string filename = name.c_str();
    return string((std::regex_replace(filename, std::regex("::"), "/")).c_str());
  };
  auto getFileName = [](const string &name) {
    std::string filename = name.c_str();
    return string((std::regex_replace(filename, std::regex("::"), "/") + ".ts").c_str());
  };

  std::function<string(const BindingBase *)> formatType =
      [&formatType](const BindingBase *type) {
        if (type->type == BindingType::Array) {
          const Array<BindingBase> *array = static_cast<const Array<BindingBase> *>(type);
          return string(formatType(array->arrayType)) + "[]";
        }
        std::string filename = type->name.c_str();
        int i = filename.find_last_of(':');
        if (i >= 0) {
          return string(filename.substr(i + 1, filename.size() - i - 1).c_str());
        }
        return string((std::regex_replace(filename, std::regex("::"), ".")).c_str());
      };

  auto formatImport = [&getModuleName, formatType](const BindingBase *type,
                                                   string filename) {
    string s;
    s += "import {" + formatType(type) + "} from \"" +
         path::relative(path::dirname(filename), getModuleName(type->name)) + "\";";
    return s;
  };

  auto formatTemplate = [&formatImport, &formatType](const BindingBase *type,
                                                     Set<string> &imports,
                                                     string &filename,
                                                     bool isDecl = false) {
    if (type->type == BindingType::Struct) {
      const _StructBase *st = static_cast<const _StructBase *>(type);
      if (st->templateParams.size() == 0) {
        return string("");
      }
      string s = "<";
      int i = 0;
      for (auto &param : st->templateParams) {
        if (isDecl && param.type->type == BindingType::Literal) {
          const LiteralType *lit = static_cast<const LiteralType *>(param.type);
          s += param.name + " extends ";

          switch (lit->litType) {
          case LitType::Bool:
            s += "boolean";
            break;
          case LitType::Number:
            s += "number";
            break;
          case LitType::String:
            s += "string";
            break;
          default:
            s += "unknown";
          }
        } else if (!isDecl && param.type->type == BindingType::Literal) {
          const LiteralType *lit = static_cast<const LiteralType *>(param.type);

          switch (lit->litType) {
          case LitType::Bool: {
            const BoolLitType *lit2 = static_cast<const BoolLitType *>(lit);
            s += lit2->data ? "true" : "false";
            break;
          }
          case LitType::Number: {
            const NumLitType *lit2 = static_cast<const NumLitType *>(lit);
            // TODO: use std::format
            char buf[512];
            sprintf(buf, "%d", int(lit2->data));
            s += buf;
            break;
          }
          case LitType::String: {
            const StrLitType *lit2 = static_cast<const StrLitType *>(lit);
            s += "\"" + lit2->data + "\"";
            break;
          }
          default:
            s += "unknown";
          }
        } else {
          s += isDecl ? param.name : formatType(param.type);
          if (param.type->type == BindingType::Struct) {
            imports.add(formatImport(param.type, filename));
          }
        }

        if (i < st->templateParams.size() - 1) {
          s += ", ";
        }
        i++;
      }
      return s + ">";
    }
    return string("");
  };

  for (auto type : typeMap.values()) {
    if (type->type != BindingType::Struct) {
      continue;
    }
    const types::Struct<void> *st = static_cast<const types::Struct<void> *>(type);

    string s = "";
    string filename = getFileName(type->name);
    Set<string> imports;

    s += string("export interface ") + formatType(type) +
         formatTemplate(type, imports, filename, true) + string(" {\n");
    for (auto &member : st->members) {
      string s2 = "  " + member.name + ": " + formatType(member.type);
      if (member.type->type == BindingType::Struct) {
        s2 += formatTemplate(member.type, imports, filename, false);
        imports.add(formatImport(member.type, filename));
      }
      s += s2 + "\n";
    }
    for (const types::Method *m : st->methods) {
      s += "  " + m->name + "(";
      for (size_t i = 0; i < m->params.size(); i++) {
        const auto &p = m->params[i];
        string pname = p.name.size() > 0 ? p.name : string("arg");
        if (p.name.size() == 0) {
          char buf[16];
          snprintf(buf, sizeof(buf), "%zu", i);
          pname = pname + string(buf);
        }
        if (i > 0) {
          s += ", ";
        }
        s += pname + ": " + formatType(p.type);
        if (p.type->type == BindingType::Struct) {
          s += formatTemplate(p.type, imports, filename, false);
          imports.add(formatImport(p.type, filename));
        }
      }
      s += "): ";
      if (m->returnType) {
        s += formatType(m->returnType);
        if (m->returnType->type == BindingType::Struct) {
          imports.add(formatImport(m->returnType, filename));
        }
      } else {
        s += "void";
      }
      s += "\n";
    }
    for (const types::Constructor *c : st->constructors) {
      s += "  new(";
      for (size_t i = 0; i < c->params.size(); i++) {
        const auto &p = c->params[i];
        string pname = p.name.size() > 0 ? p.name : string("arg");
        if (p.name.size() == 0) {
          char buf[16];
          snprintf(buf, sizeof(buf), "%zu", i);
          pname = pname + string(buf);
        }
        if (i > 0) {
          s += ", ";
        }
        s += pname + ": " + formatType(p.type);
        if (p.type->type == BindingType::Struct) {
          s += formatTemplate(p.type, imports, filename, false);
          imports.add(formatImport(p.type, filename));
        }
      }
      s += "): " + formatType(type) + formatTemplate(type, imports, filename, false) +
           "\n";
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
