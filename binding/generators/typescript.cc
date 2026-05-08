#include "typescript.h"
#include "../../path/path.h"
#include "../binding.h"
#include "binding_types.h"
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
using util::Set;
using util::string;
using util::Vector;

namespace {

class TypescriptGenerator {
 public:
  explicit TypescriptGenerator(Vector<const BindingBase *> &types) : rootTypes(types) {}

  util::Map<string, string> *generate();

 private:
  struct ClassRef {
    string name;
    string modulePath;
    /** full name including namespace (C++-style — used as unique key) */
    string fullName;
    /** TS-formatted template suffix (e.g. `<float3,"positions">`), empty for
     * non-templates */
    string tsTemplateSuffix;
    bool isTemplate;

    ClassRef() = default;
    ClassRef(string name,
             string modulePath,
             string fullName,
             string tsTemplateSuffix,
             bool isTemplate)
        : name(std::move(name)), modulePath(std::move(modulePath)),
          fullName(std::move(fullName)), tsTemplateSuffix(std::move(tsTemplateSuffix)),
          isTemplate(isTemplate)
    {
    }

    litestl::hash::HashInt computeHash() const
    {
      return litestl::hash::hash(name);
    }

    bool operator==(const ClassRef &other) const
    {
      return name == other.name && modulePath == other.modulePath &&
             isTemplate == other.isTemplate && fullName == other.fullName &&
             tsTemplateSuffix == other.tsTemplateSuffix;
    }
  };

  // -- state --
  Vector<const BindingBase *> &rootTypes;
  util::Map<string, const BindingBase *> typeMap;
  util::Map<string, string> *files = nullptr;
  Set<ClassRef> classRefs;
  Set<string> classRefImports;
  string classRefFilename;

  // -- traversal / formatting --
  void recurse(const BindingBase *type);
  string formatType(const BindingBase *type,
                    bool addTemplateSuffix = false,
                    bool transformSpecial = true);
  string formatTemplate(const BindingBase *type,
                        Set<string> &imports,
                        const string &filename,
                        bool isDecl = false);

  // -- imports --
  /** Returns an import statement, or empty string if none should be emitted. */
  string formatImport(const BindingBase *type, const string &filename);
  void addImport(const BindingBase *type, Set<string> &imports, const string &filename);

  // -- per-file builders --
  string buildEnumDecl(const types::Enum *e);
  void buildStructFile(const types::Struct<void> *st);
  string buildHelpersFile();

  // -- parameter list helper, shared by methods + constructors --
  template<typename ParamT>
  void emitParamList(const Vector<ParamT> &params,
                     string &out,
                     Set<string> &imports,
                     const string &filename);

  // -- pure helpers --
  static bool isSpecialStruct(const string &s);
  static bool isSpecialStruct(const BindingBase *type);
  static string getModuleName(const string &name);
  static string getFileName(const string &name);
  static string escapeString(const string &name);
};

bool TypescriptGenerator::isSpecialStruct(const string &s)
{
  if (s.starts_with("litestl::util::Vector<")) {
    return true;
  }
  if (s.starts_with("litestl::util::String")) {
    return true;
  }
  return false;
}

bool TypescriptGenerator::isSpecialStruct(const BindingBase *type)
{
  if (type->type == BindingType::Struct) {
    const types::Struct<void> *st = static_cast<const types::Struct<void> *>(type);
    return isSpecialStruct(st->buildFullName());
  }
  return false;
}

string TypescriptGenerator::getModuleName(const string &name)
{
  std::string filename = name.c_str();
  return string((std::regex_replace(filename, std::regex("::"), "/")).c_str());
}

string TypescriptGenerator::getFileName(const string &name)
{
  std::string filename = name.c_str();
  return string((std::regex_replace(filename, std::regex("::"), "/") + ".ts").c_str());
}

string TypescriptGenerator::escapeString(const string &name)
{
  std::string filename = name.c_str();
  return string((std::regex_replace(filename, std::regex("\""), "\\\"")).c_str());
}

string TypescriptGenerator::formatType(const BindingBase *type,
                                       bool addTemplateSuffix,
                                       bool transformSpecial)
{
  using namespace litestl::binding::types;
  if (type->type == BindingType::Pointer) {
    string s = formatType(
        static_cast<const Pointer *>(type)->ptrType, addTemplateSuffix, transformSpecial);
    if (!static_cast<const Pointer *>(type)->isNonNull) {
      s += " | undefined";
    }
    return s;
  }
  if (type->type == BindingType::Reference) {
    return formatType(static_cast<const Reference *>(type)->refType,
                      addTemplateSuffix,
                      transformSpecial);
  }

  if (transformSpecial && type->type == BindingType::Struct) {
    const types::Struct<void> *st = static_cast<const types::Struct<void> *>(type);
    if (st->buildFullName().starts_with("litestl::util::Vector<")) {
      return formatType(st->templateParams[0].type, true) + "[]";
    }
    if (st->buildFullName().starts_with("litestl::util::String")) {
      return "string";
    }
  }
  if (type->type == BindingType::Array) {
    const Array<BindingBase> *array = static_cast<const Array<BindingBase> *>(type);
    return string(formatType(array->arrayType, true)) + "[]";
  }
  if (type->type == BindingType::Union) {
    const Union *u = static_cast<const Union *>(type);
    // TODO: emit a proper discriminated union of the constituent struct types
    Vector<string> typeStrs;
    for (auto &pair : u->structs) {
      typeStrs.append(formatType(pair.type, true, transformSpecial));
    }
    return typeStrs.join("|");
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
}

void TypescriptGenerator::recurse(const BindingBase *type)
{
  if (typeMap.contains(type->buildFullName())) {
    return;
  }

  switch (type->type) {
  case BindingType::Struct: {
    const types::Struct<void> *st = static_cast<const types::Struct<void> *>(type);
    typeMap.add(type->buildFullName(), type);

    for (auto &member : st->members) {
      recurse(member.type);
    }
    for (auto &param : st->templateParams) {
      recurse(param.type);
    }
    for (const types::Method *m : st->methods) {
      if (m->returnType) {
        recurse(m->returnType);
      }
      for (const auto &p : m->params) {
        recurse(p.type);
      }
    }
    for (const types::Constructor *c : st->constructors) {
      for (const auto &p : c->params) {
        recurse(p.type);
      }
    }
    break;
  }
  case BindingType::Array:
    typeMap.add(type->name, type);
    recurse(static_cast<const types::Array<void> *>(type)->arrayType);
    break;
  case BindingType::Pointer:
    typeMap.add(type->name, type);
    recurse(static_cast<const types::Pointer *>(type)->ptrType);
    break;
  case BindingType::Reference:
    typeMap.add(type->name, type);
    recurse(static_cast<const types::Reference *>(type)->refType);
    break;
  case BindingType::Enum:
    typeMap.add(type->name, type);
    break;
  case BindingType::Union: {
    const types::Union *u = static_cast<const types::Union *>(type);
    typeMap.add(type->name, type);
    recurse(u->disPropType);
    for (auto &pair : u->structs) {
      recurse(pair.type);
    }
    break;
  }
  default:
    break;
  }
}

string TypescriptGenerator::formatImport(const BindingBase *type, const string &filename)
{
  // Vector<X> / String have no module to import; skip silently. This is the
  // single chokepoint for import construction, so guarding here lets callers
  // stay simple.
  if (isSpecialStruct(type)) {
    return string("");
  }
  // Use the raw, unqualified TS type name — never the expanded `T[]` form
  // that `transformSpecial=true` would produce for Vector<T>.
  string typeName = formatType(type, /*addTemplateSuffix=*/false, /*transformSpecial=*/false);
  return "import type {" + typeName + "} from \"" +
         path::relative(path::dirname(filename), getModuleName(type->name)) + "\";";
}

void TypescriptGenerator::addImport(const BindingBase *type,
                                    Set<string> &imports,
                                    const string &filename)
{
  switch (type->type) {
  case BindingType::Struct:
  case BindingType::Enum: {
    string s = formatImport(type, filename);
    if (s.size() > 0) {
      imports.add(s);
    }
    break;
  }
  case BindingType::Reference:
    addImport(static_cast<const types::Reference *>(type)->refType, imports, filename);
    break;
  case BindingType::Pointer:
    addImport(static_cast<const types::Pointer *>(type)->ptrType, imports, filename);
    break;
  case BindingType::Array:
    addImport(static_cast<const types::Array<const BindingBase *> *>(type)->arrayType,
              imports,
              filename);
    break;
  default:
    break;
  }
}

string TypescriptGenerator::formatTemplate(const BindingBase *type,
                                           Set<string> &imports,
                                           const string &filename,
                                           bool isDecl)
{
  using namespace litestl::binding::types;

  if (isSpecialStruct(type)) {
    const _StructBase *st = static_cast<const _StructBase *>(type);
    if (st->buildFullName().starts_with("litestl::util::Vector<")) {
      addImport(st->templateParams[0].type, imports, filename);
    }
    return "";
  }

  if (type->type != BindingType::Struct) {
    return string("");
  }

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
    }
    else if (!isDecl && param.type->type == BindingType::Literal) {
      const LiteralType *lit = static_cast<const LiteralType *>(param.type);
      switch (lit->litType) {
      case LitType::Bool: {
        const BoolLitType *lit2 = static_cast<const BoolLitType *>(lit);
        s += lit2->data ? "true" : "false";
        break;
      }
      case LitType::Number: {
        const NumLitType *lit2 = static_cast<const NumLitType *>(lit);
        char buf[512];
        snprintf(buf, sizeof(buf), "%d", int(lit2->data));
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
    }
    else {
      s += isDecl ? param.name : formatType(param.type);
      addImport(param.type, imports, filename);
    }

    if (i < st->templateParams.size() - 1) {
      s += ",";
    }
    i++;
  }
  return s + ">";
}

string TypescriptGenerator::buildEnumDecl(const types::Enum *e)
{
  string content = "export enum " + path::basename(getModuleName(e->name)) + " {\n";
  for (auto &item : e->items) {
    content += string("  ") + item.name + " = " + std::to_string(item.value) + ",\n";
  }
  content += "}\n";
  return content;
}

template<typename ParamT>
void TypescriptGenerator::emitParamList(const Vector<ParamT> &params,
                                        string &out,
                                        Set<string> &imports,
                                        const string &filename)
{
  for (size_t i = 0; i < params.size(); i++) {
    const auto &p = params[i];
    string pname = p.name.size() > 0 ? p.name : string("arg");
    if (p.name.size() == 0) {
      char buf[16];
      snprintf(buf, sizeof(buf), "%zu", i);
      pname = pname + string(buf);
    }
    if (i > 0) {
      out += ", ";
    }
    out += pname + ": " + formatType(p.type);
    if (p.type->type == BindingType::Struct) {
      out += formatTemplate(p.type, imports, filename, false);
    }
    addImport(p.type, imports, filename);
  }
}

void TypescriptGenerator::buildStructFile(const types::Struct<void> *st)
{
  const BindingBase *type = st;
  string s = header;
  string filename = getFileName(type->name);
  Set<string> imports;

  // Vector<T> / String are TS-mapped (T[] / string), not standalone classes —
  // don't register them as ClassRefs.
  if (!isSpecialStruct(type)) {
    classRefs.add({formatType(type, false, false),
                   getModuleName(type->name),
                   st->buildFullName(),
                   formatTemplate(type, classRefImports, classRefFilename, false),
                   st->templateParams.size() > 0});
  }

  s += string("export interface ") + formatType(type, false, false) +
       formatTemplate(type, imports, filename, true) + string(" {\n");
  s += "  [Symbol.dispose](): void;\n";

  for (auto &member : st->members) {
    string s2 = "  " + member.name + ": " + formatType(member.type);
    if (member.type->type == BindingType::Struct) {
      if (!isSpecialStruct(member.type)) {
        classRefs.add({formatType(member.type),
                       getModuleName(member.type->name),
                       member.type->buildFullName(),
                       formatTemplate(member.type, classRefImports, classRefFilename, false),
                       false});
      }
      s2 += formatTemplate(member.type, imports, filename, false);
    }
    addImport(member.type, imports, filename);
    s += s2 + "\n";
  }

  for (const types::Method *m : st->methods) {
    s += "  " + m->name + "(";
    emitParamList(m->params, s, imports, filename);
    s += "): ";
    if (m->returnType) {
      s += formatType(m->returnType);
      addImport(m->returnType, imports, filename);
    }
    else {
      s += "void";
    }
    s += "\n";
  }

  for (const types::Constructor *c : st->constructors) {
    s += "  new(";
    emitParamList(c->params, s, imports, filename);
    s += "): " + formatType(type) + formatTemplate(type, imports, filename, false) + "\n";
  }
  s += "}\n";

  string importString = "";
  for (auto &import : imports) {
    importString += import + "\n";
  }
  s = importString + "\n" + s;

  files->add(filename, s);
}

string TypescriptGenerator::buildHelpersFile()
{
  string helpers = header + "\n";
  Set<string> importSet;
  Set<string> exportSet;

  for (auto &ref : classRefs) {
    importSet.add("import type {" + ref.name + "} from \"./" + ref.modulePath + "\";");
  }
  for (auto &str : classRefImports) {
    importSet.add(str);
  }

  for (auto &ref : classRefs) {
    exportSet.add("export type {" + ref.name + "} from \"./" + ref.modulePath + "\";");
  }

  for (auto &str : importSet) {
    helpers += str + "\n";
  }

  helpers += "\n";
  for (auto &str : exportSet) {
    helpers += str + "\n";
  }

  // A given concrete type can land in classRefs twice: once as a top-level
  // struct (isTemplate reflects whether it has template params, so concrete
  // instantiations like `BuiltinAttr<int,"select">` come in as
  // isTemplate=true) and once as a member reference (isTemplate=false). For
  // AllBoundTypes we want the non-template copy when both exist; otherwise
  // the entry would be dropped depending on Set iteration order.
  util::Map<string, ClassRef> byFullName;
  for (auto &ref : classRefs) {
    ClassRef *existing = byFullName.lookup_ptr(ref.fullName);
    if (!existing || existing->isTemplate) {
      byFullName.add_overwrite(ref.fullName, ref);
    }
  }

  helpers += "\n/** Note: Does not include templates */\n";
  helpers += "export type AllBoundTypes = {\n";
  for (auto &ref : byFullName.values()) {
    if (!ref.isTemplate) {
      helpers += "  \"" + escapeString(ref.fullName) + "\": " + ref.name +
                 ref.tsTemplateSuffix + ",\n";
    }
  }
  helpers += "};\n";
  return helpers;
}

util::Map<string, string> *TypescriptGenerator::generate()
{
  files = alloc::New<util::Map<string, string>>("generateTypescript result");

  for (const auto type : rootTypes) {
    recurse(type);
  }

  for (auto type : typeMap.values()) {
    if (type->type == BindingType::Enum) {
      const types::Enum *enumType = static_cast<const types::Enum *>(type);
      string s = header;
      s += buildEnumDecl(enumType);
      files->add(getFileName(type->name), s);
    }
  }

  for (auto type : typeMap.values()) {
    if (type->type == BindingType::Struct) {
      buildStructFile(static_cast<const types::Struct<void> *>(type));
    }
  }

  files->add("index.ts", buildHelpersFile());
  return files;
}

} // namespace

util::Map<string, string> *generateTypescript(Vector<const BindingBase *> &types)
{
  TypescriptGenerator gen(types);
  return gen.generate();
}

} // namespace litestl::binding::generators
