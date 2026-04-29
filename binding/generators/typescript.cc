/** TODO: clean up this rather messy file. */

#include "typescript.h"
#include "../../path/path.h"
#include "../binding.h"
#include "binding_types.h"
#include "util/function.h"
#include "util/set.h"
#include <algorithm>
#include <regex>
#include <string>

static const litestl::util::string basicTSTypeDefs = R"(
type float = number;
type pointer<T=any> = number;
type int = number;
type uint = number;
type double = number;
type short = number;
type ushort = number;
type char = number;
type uchar = number;
)";

static const litestl::util::string header = R"(/** Auto-generated file */
/* eslint-disable @typescript-eslint/no-misused-new */
/* eslint-disable @typescript-eslint/no-unused-vars */
)" + basicTSTypeDefs;

namespace litestl::binding::generators {
using util::function_ref;
using util::Set;
using util::string;
using util::Vector;

struct TypescriptType {
  string name;
  Vector<TypescriptType *> typeParams;
};

static bool isSpecialStruct(const litestl::util::string s)
{
  if (s.starts_with("litestl::util::Vector<")) {
    return true;
  }
  if (s.starts_with("litestl::util::String")) {
    return true;
  }
  return false;
}

static bool isSpecialStruct(const BindingBase *type)
{
  using namespace litestl::binding::types;
  if (type->type == BindingType::Struct) {
    const types::Struct<void> *st = static_cast<const types::Struct<void> *>(type);
    return isSpecialStruct(st->buildFullName());
  }
  return false;
}

static string formatType(const BindingBase *type, bool addTemplateSuffix = false)
{
  using namespace litestl::binding::types;
  if (type->type == BindingType::Struct) {
    const types::Struct<void> *st = static_cast<const types::Struct<void> *>(type);
    if (st->buildFullName().starts_with("litestl::util::Vector<")) {
      // We have special support for Vectors
      return formatType(st->templateParams[0].type, true) + "[]";
    }
    if (st->buildFullName().starts_with("litestl::util::String")) {
      // We have special support for Strings
      return "string";
    }
  }
  if (type->type == BindingType::Array) {
    const Array<BindingBase> *array = static_cast<const Array<BindingBase> *>(type);
    return string(formatType(array->arrayType, true)) + "[]";
  }
  if (type->type == BindingType::Union) {
    /* TODO: emit a proper discriminated union of the constituent struct types. */
    return "unknown";
  }
  std::string filename = type->name.c_str();
  if (addTemplateSuffix && type->type == BindingType::Struct) {
    filename = static_cast<const _StructBase *>(type)->buildFullName();
  }
  int i = filename.find_last_of(':');
  if (i >= 0) {
    return string(filename.substr(i + 1, filename.size() - i - 1).c_str());
  }
  return string((std::regex_replace(filename, std::regex("::"), ".")).c_str());
};

static string getModuleName(const string &name)
{
  std::string filename = name.c_str();
  return string((std::regex_replace(filename, std::regex("::"), "/")).c_str());
};

static string buildEnum(const types::Enum *e)
{
  string content = "export enum " + path::basename(getModuleName(e->name)) + " {\n";
  for (auto &item : e->items) {
    content += string("  ") + item.name + " = " + std::to_string(item.value) + ",\n";
  }
  content += "}\n";
  return content;
}

static void recurse(const BindingBase *type,
                    util::Map<string, const BindingBase *> &typeMap)
{
  if (typeMap.contains(type->buildFullName())) {
    return;
  }

  switch (type->type) {
  case BindingType::Struct: {
    const types::Struct<void> *st = static_cast<const types::Struct<void> *>(type);
    typeMap.add(type->buildFullName(), type);

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
    break;
  }
  case BindingType::Array: {
    typeMap.add(type->name, type);
    recurse(static_cast<const types::Array<void> *>(type)->arrayType, typeMap);
    break;
  }
  case BindingType::Pointer: {
    typeMap.add(type->name, type);
    recurse(static_cast<const types::Pointer *>(type)->ptrType, typeMap);
    break;
  }
  case BindingType::Reference: {
    typeMap.add(type->name, type);
    recurse(static_cast<const types::Reference *>(type)->refType, typeMap);
    break;
  }
  case BindingType::Enum: {
    typeMap.add(type->name, type);
    break;
  }
  case BindingType::Union: {
    const types::Union<int> *u = static_cast<const types::Union<int> *>(type);
    typeMap.add(type->name, type);
    recurse(u->disPropType, typeMap);
    for (auto &pair : u->structs) {
      recurse(pair.type, typeMap);
    }
    break;
  }
  default:
    break;
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

  struct ClassRef {
    string name;
    string modulePath;
    /** full name including namespace (C++-style — used as unique key) */
    string fullName;
    /** TS-formatted template suffix (e.g. `<float3,"positions">`), empty for
     * non-templates */
    string tsTemplateSuffix;
    bool isTemplate;

    bool operator==(const ClassRef &other) const
    {
      return name == other.name && modulePath == other.modulePath &&
             isTemplate == other.isTemplate && fullName == other.fullName &&
             tsTemplateSuffix == other.tsTemplateSuffix;
    }
  };
  Set<string> classRefImports;
  string classRefFilename = "";

  // used to build various larger helper type unions and mapped types
  Vector<ClassRef> classRefs;

  auto escapeString = [](const string &name) {
    std::string filename = name.c_str();
    return string((std::regex_replace(filename, std::regex("\""), "\\\"")).c_str());
  };
  auto getFileName = [](const string &name) {
    std::string filename = name.c_str();
    return string((std::regex_replace(filename, std::regex("::"), "/") + ".ts").c_str());
  };

  auto formatImport = [](const BindingBase *type, string filename) {
    string s;
    s += "import type {" + formatType(type) + "} from \"" +
         path::relative(path::dirname(filename), getModuleName(type->name)) + "\";";
    return s;
  };

  /** recursively adds imports */
  std::function<void(const BindingBase *type, Set<string> &imports, string &filename)>
      addImport = [&formatImport, &addImport](
                      const BindingBase *type, Set<string> &imports, string &filename) {
        if (type->type == BindingType::Struct) {
          if (!isSpecialStruct(type)) {
            imports.add(formatImport(type, filename));
          }
        } else if (type->type == BindingType::Reference) {
          addImport(
              static_cast<const types::Reference *>(type)->refType, imports, filename);
        } else if (type->type == BindingType::Pointer) {
          addImport(
              static_cast<const types::Pointer *>(type)->ptrType, imports, filename);
        } else if (type->type == BindingType::Array) {
          addImport(
              static_cast<const types::Array<const BindingBase *> *>(type)->arrayType,
              imports,
              filename);
        }
      };

  auto formatTemplate = [&formatImport, &addImport](const BindingBase *type,
                                                    Set<string> &imports,
                                                    string &filename,
                                                    bool isDecl = false) -> string {
    // check if we are one of the specially handled object types
    if (isSpecialStruct(type)) {
      const _StructBase *st = static_cast<const _StructBase *>(type);
      if (st->buildFullName().starts_with("litestl::util::Vector<")) {
        addImport(st->templateParams[0].type, imports, filename);
      }
      return "";
    }

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
          if (param.type->type == BindingType::Struct && !isSpecialStruct(param.type)) {
            imports.add(formatImport(param.type, filename));
          }
        }

        if (i < st->templateParams.size() - 1) {
          s += ",";
        }
        i++;
      }
      return s + ">";
    }
    return string("");
  };

  for (auto type : typeMap.values()) {
    if (type->type == BindingType::Enum) {
      const types::Enum *enumType = static_cast<const types::Enum *>(type);
      string s = header;
      string filename = getFileName(type->name);
      s += buildEnum(enumType);
      files->add(filename, s);
    }
  }
  for (auto type : typeMap.values()) {
    if (type->type != BindingType::Struct) {
      continue;
    }
    if (isSpecialStruct(type)) {
      continue;
    }
    const types::Struct<void> *st = static_cast<const types::Struct<void> *>(type);

    string s = header;
    string filename = getFileName(type->name);
    Set<string> imports;

    classRefs.append_once({formatType(type),
                           getModuleName(type->name),
                           st->buildFullName(),
                           formatTemplate(type, classRefImports, classRefFilename, false),
                           st->templateParams.size() > 0});

    s += string("export interface ") + formatType(type) +
         formatTemplate(type, imports, filename, true) + string(" {\n");
    s += "  [Symbol.dispose](): void;\n";
    for (auto &member : st->members) {
      string s2 = "  " + member.name + ": " + formatType(member.type);
      if (member.type->type == BindingType::Struct) {
        classRefs.append_once(
            {formatType(member.type),
             getModuleName(member.type->name),
             member.type->buildFullName(),
             formatTemplate(member.type, classRefImports, classRefFilename, false),
             false});

        s2 += formatTemplate(member.type, imports, filename, false);

        if (!isSpecialStruct(member.type)) {
          imports.add(formatImport(member.type, filename));
        }
      } else if (member.type->type == BindingType::Enum) {
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
          if (!isSpecialStruct(p.type)) {
            imports.add(formatImport(p.type, filename));
          }
        } else if (p.type->type == BindingType::Enum) {
          imports.add(formatImport(p.type, filename));
        }
      }
      s += "): ";
      if (m->returnType) {
        s += formatType(m->returnType);
        if (m->returnType->type == BindingType::Struct && !isSpecialStruct(m->returnType))
        {
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
          if (!isSpecialStruct(p.type)) {
            imports.add(formatImport(p.type, filename));
          }
        } else if (p.type->type == BindingType::Enum) {
          imports.add(formatImport(p.type, filename));
        }
      }
      s += "): " + formatType(type) + formatTemplate(type, imports, filename, false) +
           "\n";
    }
    s += "}\n";

    string importString = "";
    for (auto &import : imports) {
      // XXX track down where this happens
      if (import.contains("[")) {
        continue;
      }
      importString += import + "\n";
    }
    s = importString + "\n" + s;

    files->add(filename, s);
  }

  auto buildHelpers = [&classRefs, &escapeString, &classRefImports]() -> string {
    string helpers = header + "\n";
    Set<string> importSet;
    Set<string> exportSet;

    classRefs = classRefs.filter([](const ClassRef &ref) -> bool { //
      return !isSpecialStruct(ref.fullName);
    });

    // build imports
    for (auto &ref : classRefs) {
      string modulePath = ref.modulePath;
      importSet.add("import type {" + ref.name + "} from \"./" + modulePath + "\";");
    }
    for (auto &str : classRefImports) {
      importSet.add(str);
    }

    // build exports
    for (auto &ref : classRefs) {
      string modulePath = ref.modulePath;
      exportSet.add("export type {" + ref.name + "} from \"./" + modulePath + "\";");
    }

    // imports
    for (auto &str : importSet) {
      // XXX track down where this happens
      if (str.contains("[")) {
        continue;
      }
      helpers += str + "\n";
    }

    // exports
    helpers += "\n";
    for (auto &str : exportSet) {
      // XXX track down where this happens
      if (str.contains("[")) {
        continue;
      }
      helpers += str + "\n";
    }

    helpers += "\n/** Note: Does not include templates */\n";
    helpers += "export type AllBoundTypes = {\n";
    for (auto &ref : classRefs) {
      if (!ref.isTemplate) {
        helpers += "  \"" + escapeString(ref.fullName) + "\": " + ref.name +
                   ref.tsTemplateSuffix + ",\n";
      }
    }
    helpers += "};\n";

    return helpers;
  };

  files->add("index.ts", buildHelpers());

  return files;
}
} // namespace litestl::binding::generators
