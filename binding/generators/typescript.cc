#include "typescript.h"
#include "../../path/path.h"
#include "../binding.h"
#include "binding_types.h"
#include "util/set.h"
#include <algorithm>
#include <regex>
#include <string>

static const litestl::util::string basicTSTypeDefs = R"(
type pointer<T=any> = number;
type int8 = number;
type uint8 = number;
type int16 = number;
type uint16 = number;
type int32 = number;
type uint32 = number;
type int64 = number;
type uint64 = number;
type float = number;
type double = number;

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
  explicit TypescriptGenerator(Vector<const BindingBase *> &types) : rootTypes(types)
  {
  }

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
  void buildUnionMapFile(const types::Union *u);
  string buildHelpersFile();

  /** Find the Union (in typeMap) whose `disPropType` is the given enum.
   * Used to resolve a `ParentTemplateParam::disambiguator` enum back to the
   * mapped-type that the TS generator emits for that union. */
  const types::Union *findUnionForEnum(const types::Enum *e) const;

  // -- parameter list helper, shared by methods + constructors --
  template <typename ParamT>
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
      return "(" + formatType(st->templateParams[0].type, true) + ")[]";
    }
    if (st->buildFullName().starts_with("litestl::util::String")) {
      return "string";
    }
  }
  if (type->type == BindingType::Array) {
    const Array<BindingBase> *array = static_cast<const Array<BindingBase> *>(type);
    return "(" + formatType(array->arrayType, true) + ")[]";
  }
  if (type->type == BindingType::Union) {
    const Union *u = static_cast<const Union *>(type);
    const Enum *enumKey = u->disPropType->type == BindingType::Enum ?
                              static_cast<const Enum *>(u->disPropType) :
                              nullptr;
    Vector<string> typeStrs;
    for (auto &pair : u->structs) {
      string s = formatType(pair.type, true, transformSpecial);

      // If the variant opts into disambiguation, the variant's interface has a
      // TS-only `K` parameter (see buildStructFile). Append the variant's
      // enum-key name as a 2nd template arg so the union element types match
      // their interface declarations.
      if (enumKey) {
        bool hasDis = false;
        for (auto &member : pair.type->members) {
          if (member.type->type != BindingType::ParentTemplParam) {
            continue;
          }
          if (static_cast<const ParentTemplateParam *>(member.type)->disambiguator ==
              enumKey) {
            hasDis = true;
            break;
          }
        }
        if (hasDis) {
          int32_t key = *reinterpret_cast<const int32_t *>(pair.typeValue);
          string enumShort = formatType(enumKey);
          string keyName;
          for (auto &item : enumKey->items) {
            if (item.value == key) {
              keyName = item.name;
              break;
            }
          }
          if (keyName.size() > 0) {
            std::string ts = s.c_str();
            // splice ", <Enum.KEY>" before the closing `>` (always present
            // because the variant has at least the C++ template param).
            ts = ts.substr(0, ts.size() - 1) + "," +
                 std::string(enumShort.c_str()) + "." + std::string(keyName.c_str()) +
                 ">";
            s = string(ts.c_str());
          }
        }
      }
      typeStrs.append(s);
    }
    return typeStrs.join("|");
  }

  // Strip the namespace from the class name *first*, then recursively format
  // the template arguments. Using buildFullName() naively would smash the
  // qualified arg names (e.g. `litestl::math::float2`) into the same string
  // and let the basename search trip over a `:` inside the template suffix.
  std::string filename = type->name.c_str();
  int i = filename.find_last_of(':');
  string base = (i >= 0) ? string(filename.substr(i + 1).c_str()) :
                           string(std::regex_replace(filename, std::regex("::"), ".").c_str());

  if (addTemplateSuffix && type->type == BindingType::Struct) {
    const _StructBase *st = static_cast<const _StructBase *>(type);
    if (st->templateParams.size() > 0) {
      string s = base + "<";
      for (size_t j = 0; j < st->templateParams.size(); j++) {
        if (j > 0) {
          s += ",";
        }
        s += formatType(st->templateParams[j].type, true, transformSpecial);
      }
      s += ">";
      return s;
    }
  }
  return base;
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
  if (getFileName(type->name) == filename) {
    return string("");
  }

  // Vector<X> / String have no module to import; skip silently. This is the
  // single chokepoint for import construction, so guarding here lets callers
  // stay simple.
  if (isSpecialStruct(type)) {
    return string("");
  }
  // Use the raw, unqualified TS type name — never the expanded `T[]` form
  // that `transformSpecial=true` would produce for Vector<T>.
  string typeName =
      formatType(type, /*addTemplateSuffix=*/false, /*transformSpecial=*/false);
  return "import type {" + typeName + "} from \"" +
         path::relative(path::dirname(filename), getModuleName(type->name)) + "\";";
}

void TypescriptGenerator::addImport(const BindingBase *type,
                                    Set<string> &imports,
                                    const string &filename)
{
  using namespace binding::types;

  switch (type->type) {
  case BindingType::Struct: {
    // Special structs (Vector<T>, String) print as `T[]` / `string` in
    // formatType, so the template arg name leaks into the output and needs
    // its own import even though the wrapper itself doesn't.
    if (isSpecialStruct(type)) {
      const _StructBase *st = static_cast<const _StructBase *>(type);
      if (st->buildFullName().starts_with("litestl::util::Vector<")) {
        addImport(st->templateParams[0].type, imports, filename);
      }
      break;
    }
    string s = formatImport(type, filename);
    if (s.size() > 0) {
      imports.add(s);
    }
    // When formatType emits a template suffix (e.g. via the Union join path
    // or any addTemplateSuffix=true caller), the arg names leak into the
    // output and need their own imports too.
    const _StructBase *st = static_cast<const _StructBase *>(type);
    for (auto &tp : st->templateParams) {
      addImport(tp.type, imports, filename);
    }
    break;
  }
  case BindingType::Enum: {
    string s = formatImport(type, filename);
    if (s.size() > 0) {
      imports.add(s);
    }
    break;
  }
  case BindingType::Union: {
    const Union *u = static_cast<const Union *>(type);
    for (auto &item : u->structs) {
      addImport(item.type, imports, filename);
    }
    // The Union join in `formatType` may emit the discriminator enum value
    // as a per-variant template arg (e.g. `UniformDef<float, UniformBindType.FLOAT>`),
    // so any file consuming a Union needs the enum's import.
    if (u->disPropType) {
      addImport(u->disPropType, imports, filename);
    }
    break;
  }
  case BindingType::Reference:
    addImport(static_cast<const Reference *>(type)->refType, imports, filename);
    break;
  case BindingType::Pointer:
    addImport(static_cast<const Pointer *>(type)->ptrType, imports, filename);
    break;
  case BindingType::Array:
    addImport(static_cast<const Array<const BindingBase *> *>(type)->arrayType,
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
      // Vector maps to `T[]` everywhere it's *used*, so the suffix is empty
      // there. But the standalone interface declaration still references the
      // element-type param `T` (its constructor takes `ptrWithCount(T*, int)`),
      // so the declaration must introduce that generic parameter.
      if (isDecl) {
        return "<T = any>";
      }
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
    } else {
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

const types::Union *TypescriptGenerator::findUnionForEnum(const types::Enum *e) const
{
  for (auto type : typeMap.values()) {
    if (type->type != BindingType::Union) {
      continue;
    }
    const types::Union *u = static_cast<const types::Union *>(type);
    if (u->disPropType == static_cast<const BindingBase *>(e)) {
      return u;
    }
  }
  return nullptr;
}

void TypescriptGenerator::buildUnionMapFile(const types::Union *u)
{
  using namespace litestl::binding::types;
  if (u->disPropType->type != BindingType::Enum) {
    return;
  }
  const Enum *e = static_cast<const Enum *>(u->disPropType);
  string filename = getFileName(u->mapName);

  // Locate the shared C++ template-param name discriminated by this enum.
  // Pick the first variant's `ParentTemplateParam` whose `disambiguator`
  // matches `e`; assume that name is consistent across variants.
  string disParamName;
  for (auto &pair : u->structs) {
    for (auto &member : pair.type->members) {
      if (member.type->type != BindingType::ParentTemplParam) {
        continue;
      }
      const ParentTemplateParam *p =
          static_cast<const ParentTemplateParam *>(member.type);
      if (p->disambiguator == e) {
        disParamName = p->templParamName;
        break;
      }
    }
    if (disParamName.size() > 0) {
      break;
    }
  }
  if (disParamName.size() == 0) {
    // No variant opts into disambiguation — there's nothing to map. Skip
    // emission entirely so we don't litter `typescript/` with empty
    // `export type Foo = {}` files for every Union.
    return;
  }

  string enumShortName = formatType(e); // already strips namespace
  string mapShortName;
  {
    std::string fullMapName = u->mapName.c_str();
    auto i = fullMapName.find_last_of(':');
    mapShortName = (i != std::string::npos) ?
                       string(fullMapName.substr(i + 1).c_str()) :
                       string(fullMapName.c_str());
  }

  Set<string> imports;
  imports.add("import type {" + enumShortName + "} from \"" +
              path::relative(path::dirname(filename), getModuleName(e->name)) + "\";");

  string body = "export type " + mapShortName + " = {\n";
  for (auto &pair : u->structs) {
    // Locate the variant's concrete type for the disambiguated template param.
    const BindingBase *concrete = nullptr;
    for (auto &tp : pair.type->templateParams) {
      if (tp.name == disParamName) {
        concrete = tp.type;
        break;
      }
    }
    if (!concrete) {
      // Variant doesn't carry the expected param — skip.
      continue;
    }
    string variantTypeStr = formatType(concrete, true);
    addImport(concrete, imports, filename);

    // Read the runtime enum value back out of UnionPair::typeValue.
    int32_t key = *reinterpret_cast<const int32_t *>(pair.typeValue);
    string keyName;
    for (auto &item : e->items) {
      if (item.value == key) {
        keyName = item.name;
        break;
      }
    }
    if (keyName.size() == 0) {
      continue;
    }
    body += "  [" + enumShortName + "." + keyName + "]: " + variantTypeStr + ",\n";
  }
  body += "}\n";

  string importString;
  for (auto &imp : imports) {
    importString += imp + "\n";
  }
  files->add(filename, header + importString + "\n" + body);
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

template <typename ParamT>
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

  // Scan members for the (at most one) `ParentTemplateParam::disambiguator`
  // back-reference. When present, we add a TS-only `K extends keyof <map>`
  // template parameter, a `[getTypeSymbol]: K` discriminator field, and
  // rewrite each tagged member's type to `<map>[K]`.
  const types::Enum *disEnum = nullptr;
  string disParamName; // e.g. "T" — which C++ template param the disambiguator covers
  for (auto &member : st->members) {
    if (member.type->type != BindingType::ParentTemplParam) {
      continue;
    }
    const types::ParentTemplateParam *p =
        static_cast<const types::ParentTemplateParam *>(member.type);
    if (!p->disambiguator) {
      continue;
    }
    if (disEnum && disEnum != p->disambiguator) {
      // Multiple distinct disambiguators on one struct isn't supported yet.
      // Fall back to no disambiguation and let the user notice in the diff.
      disEnum = nullptr;
      disParamName = "";
      break;
    }
    disEnum = p->disambiguator;
    disParamName = p->templParamName;
  }

  const types::Union *disUnion = disEnum ? findUnionForEnum(disEnum) : nullptr;
  string mapShortName;
  if (disUnion) {
    mapShortName = formatType(disUnion->disPropType); // basename of enum, e.g. "UniformBindType"
    // Replace "UniformBindType" → "UniformBindTypeMap" by using the union's mapName.
    string fullMapName = disUnion->mapName;
    int i = std::string(fullMapName.c_str()).find_last_of(':');
    if (i >= 0) {
      mapShortName = string(std::string(fullMapName.c_str()).substr(i + 1).c_str());
    } else {
      mapShortName = fullMapName;
    }
  }

  string templDecl = formatTemplate(type, imports, filename, true);
  if (disUnion) {
    // Append `, K extends keyof <map>` to the existing template declaration.
    // `K` is purely a TS-side parameter — it has no C++ template counterpart.
    string extra = "K extends keyof " + mapShortName;
    if (templDecl.size() == 0) {
      templDecl = "<" + extra + ">";
    } else {
      // existing is "<A,B>" — splice before the closing `>`.
      std::string td = templDecl.c_str();
      td = td.substr(0, td.size() - 1) + "," + std::string(extra.c_str()) + ">";
      templDecl = string(td.c_str());
    }
    // Import the map and the runtime discriminator symbol.
    imports.add("import type {" + mapShortName + "} from \"" +
                path::relative(path::dirname(filename), getModuleName(disUnion->mapName)) +
                "\";");
    imports.add("import {getTypeSymbol} from \"@litestl/typescript-runtime\";");
  }

  s += string("export interface ") + formatType(type, false, false) + templDecl +
       string(" {\n");
  s += "  [Symbol.dispose](): void;\n";
  if (disUnion) {
    s += "  readonly [getTypeSymbol]: K;\n";
  }

  for (auto &member : st->members) {
    // Rewrite disambiguated `ParentTemplateParam` members to `<map>[K]`.
    if (disUnion && member.type->type == BindingType::ParentTemplParam) {
      const types::ParentTemplateParam *p =
          static_cast<const types::ParentTemplateParam *>(member.type);
      if (p->disambiguator == disEnum) {
        s += "  " + member.name + ": " + mapShortName + "[K]\n";
        continue;
      }
    }

    string s2 = "  " + member.name + ": " + formatType(member.type);
    if (member.type->type == BindingType::Struct) {
      if (!isSpecialStruct(member.type)) {
        classRefs.add(
            {formatType(member.type),
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
    } else {
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

  for (auto type : typeMap.values()) {
    if (type->type == BindingType::Union) {
      buildUnionMapFile(static_cast<const types::Union *>(type));
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
