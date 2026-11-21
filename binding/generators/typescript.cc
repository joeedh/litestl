#include "typescript.h"
#include "../../path/path.h"
#include "../binding.h"
#include "../binding_dispatch.h"
#include "binding_types.h"
#include "util/set.h"
#include <algorithm>
#include <cstring>
#include <regex>
#include <string>

// Plain char arrays rather than util::string: a global util::string allocates
// during static init, outside any alloc::PermanentGuard, and shows up as a leak.
static const char header[] = R"(/** Auto-generated file */
/* eslint-disable @typescript-eslint/no-misused-new */
/* eslint-disable @typescript-eslint/no-unused-vars */

type pointer<T=any> = number;
type int8 = number;
type uint8 = number;
type int16 = number;
type uint16 = number;
type int32 = number;
type uint32 = number;
type int64 = bigint;
type uint64 = bigint;
type float = number;
type double = number;

)";

static const char *runtimeModule = "@litestl/typescript-runtime";

namespace litestl::binding::generators {
using util::Set;
using util::string;
using util::Vector;

namespace {

using types::Method;
using types::StructTemplate;
using StructGroup = Vector<const types::_StructBase *>;

/** Where a rendered type appears; returns convert some types to JS values. */
enum class Pos { Member, Param, Return };

/**
 * The instantiations of one template, rendered together so that a position
 * holding a template parameter's value in every one of them prints as that
 * parameter. Row i holds instantiation i's template arguments; `paramText[j]`
 * is what parameter j prints as (its name, or a selector-map lookup).
 */
struct TemplCtx {
  Vector<Vector<StructTemplate>> rows;
  Vector<string> paramText;
};

/** One candidate of a callable group: a method and the struct instantiation it
 * belongs to (nullptr for free functions). */
struct Callable {
  const Method *m;
  const types::_StructBase *owner;
};

struct RenderedCallable {
  string generics;
  string params;
  string ret;
  /** `export type X_Args = {...}` for select-mode templates, else empty. */
  string mapDecl;
};

bool isReservedWord(const string &s)
{
  static const char *words[] = {
      "break",  "case",    "catch",  "class",  "const",      "continue", "debugger",
      "default", "delete", "do",     "else",   "enum",       "export",   "extends",
      "false",  "finally", "for",    "function", "if",       "import",   "in",
      "instanceof", "new", "null",   "return", "super",      "switch",   "this",
      "throw",  "true",    "try",    "typeof", "var",        "void",     "while",
      "with",   "let",     "static", "yield",  "await",      "implements", "interface",
      "package", "private", "protected", "public"};
  for (const char *w : words) {
    if (strcmp(s.c_str(), w) == 0) {
      return true;
    }
  }
  return false;
}

string shortName(const string &name)
{
  std::string s = name.c_str();
  auto i = s.rfind("::");
  return i == std::string::npos ? name : string(s.substr(i + 2).c_str());
}

bool nameIs(const string &name, const char *s)
{
  return strcmp(name.c_str(), s) == 0;
}

int compareNames(const string &a, const string &b)
{
  return strcmp(a.c_str(), b.c_str());
}

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
  Vector<const Method *> functions;
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
  string render(const Vector<const BindingBase *> &nodes,
                const TemplCtx &ctx,
                Set<string> &imports,
                const string &filename,
                Pos pos);
  string renderUnion(const Vector<const BindingBase *> &nodes,
                     Set<string> &imports,
                     const string &filename);
  RenderedCallable renderCallable(const Vector<Callable> &entries,
                                  const Vector<const Method *> &dispatchGroup,
                                  bool keyIncludesOwner,
                                  const string &mapName,
                                  Set<string> &imports,
                                  const string &filename);

  // -- imports --
  /** Returns an import statement, or empty string if none should be emitted. */
  string formatImport(const BindingBase *type, const string &filename);
  void addImport(const BindingBase *type, Set<string> &imports, const string &filename);

  // -- per-file builders --
  string buildEnumDecl(const types::Enum *e);
  void buildStructFile(const StructGroup &group);
  void buildUnionMapFile(const types::Union *u);
  void buildFunctionFiles();
  string buildHelpersFile();

  /** Find the Union (in typeMap) whose `disPropType` is the given enum.
   * Used to resolve a `ParentTemplateParam::disambiguator` enum back to the
   * mapped-type that the TS generator emits for that union. */
  const types::Union *findUnionForEnum(const types::Enum *e) const;

  // -- pure helpers --
  static bool isSpecialStruct(const string &s);
  static bool isSpecialStruct(const BindingBase *type);
  static string getModuleName(const string &name);
  static string getFileName(const string &name);
  static string escapeString(const string &name);
};

bool TypescriptGenerator::isSpecialStruct(const string &s)
{
  return s.starts_with("litestl::util::Vector<") || s.starts_with("litestl::util::String") ||
         s.starts_with("litestl::util::CString") || s.starts_with("litestl::util::Span");
}

bool TypescriptGenerator::isSpecialStruct(const BindingBase *type)
{
  if (type->type == BindingType::Struct) {
    const types::_StructBase *st = static_cast<const types::_StructBase *>(type);
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
    const types::_StructBase *st = static_cast<const types::_StructBase *>(type);
    string full = st->buildFullName();
    if (full.starts_with("litestl::util::Vector<")) {
      return "(" + formatType(st->templateParams[0].type, true) + ")[]";
    }
    if (full.starts_with("litestl::util::String") || full.starts_with("litestl::util::CString"))
    {
      return "string";
    }
    if (full.starts_with("litestl::util::Span")) {
      return "ArrayLike<" + formatType(st->templateParams[0].type, true) + ">";
    }
  }
  if (type->type == BindingType::Array) {
    const Array<BindingBase> *array = static_cast<const Array<BindingBase> *>(type);
    return "(" + formatType(array->arrayType, true) + ")[]";
  }
  if (type->type == BindingType::Union) {
    const Union *u = static_cast<const Union *>(type);
    const Enum *enumKey = u->disPropType->type == BindingType::Enum
                              ? static_cast<const Enum *>(u->disPropType)
                              : nullptr;
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
              enumKey)
          {
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
            ts = ts.substr(0, ts.size() - 1) + "," + std::string(enumShort.c_str()) +
                 "." + std::string(keyName.c_str()) + ">";
            s = string(ts.c_str());
          }
        }
      }
      typeStrs.append(s);
    }
    return typeStrs.join("|");
  }
  if (type->type == BindingType::Literal) {
    const LiteralType *lit = static_cast<const LiteralType *>(type);
    switch (lit->litType) {
    case LitType::Bool:
      return static_cast<const BoolLitType *>(lit)->data ? "true" : "false";
    case LitType::Number: {
      char buf[64];
      snprintf(buf, sizeof(buf), "%d", int(static_cast<const NumLitType *>(lit)->data));
      return buf;
    }
    case LitType::String:
      return "\"" + static_cast<const StrLitType *>(lit)->data + "\"";
    }
  }

  // Strip the namespace from the class name *first*, then recursively format
  // the template arguments. Using buildFullName() naively would smash the
  // qualified arg names (e.g. `litestl::math::float2`) into the same string
  // and let the basename search trip over a `:` inside the template suffix.
  std::string filename = type->name.c_str();
  int i = filename.find_last_of(':');
  string base = (i >= 0)
                    ? string(filename.substr(i + 1).c_str())
                    : string(std::regex_replace(filename, std::regex("::"), ".").c_str());

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
  if (type->type == BindingType::Method) {
    const Method *m = static_cast<const Method *>(type);
    if (!functions.contains(m)) {
      functions.append(m);
      if (m->returnType) {
        recurse(m->returnType);
      }
      for (const auto &p : m->params) {
        recurse(p.type);
      }
    }
    return;
  }

  if (typeMap.contains(type->buildFullName())) {
    return;
  }

  switch (type->type) {
  case BindingType::Struct: {
    const types::_StructBase *st = static_cast<const types::_StructBase *>(type);
    typeMap.add(type->buildFullName(), type);

    for (auto &member : st->members) {
      recurse(member.type);
    }
    for (auto &param : st->templateParams) {
      recurse(param.type);
    }
    for (const Method *m : st->methods) {
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
    recurse(static_cast<const types::ArrayBase *>(type)->arrayType);
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
      if (st->templateParams.size() > 0) {
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
  case BindingType::ParentTemplParam:
    addImport(
        static_cast<const ParentTemplateParam *>(type)->concreteType, imports, filename);
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
      s += formatType(param.type);
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

string TypescriptGenerator::renderUnion(const Vector<const BindingBase *> &nodes,
                                        Set<string> &imports,
                                        const string &filename)
{
  Vector<string> seen;
  for (const BindingBase *n : nodes) {
    string s = formatType(n, true);
    addImport(n, imports, filename);
    if (!seen.contains(s)) {
      seen.append(s);
    }
  }
  return seen.size() == 1 ? seen[0] : "(" + seen.join(" | ") + ")";
}

string TypescriptGenerator::render(const Vector<const BindingBase *> &nodes,
                                   const TemplCtx &ctx,
                                   Set<string> &imports,
                                   const string &filename,
                                   Pos pos)
{
  using namespace litestl::binding::types;
  const BindingBase *n0 = nodes[0];

  // A position holding parameter j's value in every instantiation is that
  // parameter. A parameter that never varies tells us nothing (with one
  // instantiation everything would "match"), so those render concretely.
  if (nodes.size() >= 2 && ctx.rows.size() == nodes.size()) {
    int match = -1;
    int count = 0;
    for (int j = 0; j < ctx.paramText.size(); j++) {
      bool all = true;
      bool varies = false;
      string first = ctx.rows[0][j].type->buildFullName();
      for (int i = 0; i < nodes.size(); i++) {
        string v = ctx.rows[i][j].type->buildFullName();
        varies = varies || v != first;
        if (nodes[i]->buildFullName() != v) {
          all = false;
          break;
        }
      }
      if (all && varies) {
        match = j;
        count++;
      }
    }
    if (count > 1) {
      printf("typescript generator: a type in %s matches more than one template "
             "parameter in every instantiation (e.g. %s); mark it explicitly with "
             "ParentTemplateParam\n",
             filename.c_str(),
             n0->buildFullName().c_str());
      abort();
    }
    if (count == 1) {
      return ctx.paramText[match];
    }
  }

  for (const BindingBase *n : nodes) {
    if (n->type != n0->type || n->name != n0->name) {
      return renderUnion(nodes, imports, filename);
    }
  }

  auto column = [&](auto getter) {
    Vector<const BindingBase *> col;
    for (const BindingBase *n : nodes) {
      col.append(getter(n));
    }
    return col;
  };

  switch (n0->type) {
  case BindingType::ParentTemplParam:
    addImport(n0, imports, filename);
    return static_cast<const ParentTemplateParam *>(n0)->templParamName;
  case BindingType::Pointer: {
    string s = render(column([](const BindingBase *n) {
                        return static_cast<const Pointer *>(n)->ptrType;
                      }),
                      ctx,
                      imports,
                      filename,
                      pos);
    if (!static_cast<const Pointer *>(n0)->isNonNull) {
      s += " | undefined";
    }
    return s;
  }
  case BindingType::Reference:
    return render(column([](const BindingBase *n) {
                    return static_cast<const Reference *>(n)->refType;
                  }),
                  ctx,
                  imports,
                  filename,
                  pos);
  case BindingType::Array:
    return "(" +
           render(column([](const BindingBase *n) {
                    return static_cast<const Array<BindingBase> *>(n)->arrayType;
                  }),
                  ctx,
                  imports,
                  filename,
                  Pos::Member) +
           ")[]";
  case BindingType::Struct: {
    const _StructBase *st0 = static_cast<const _StructBase *>(n0);
    for (const BindingBase *n : nodes) {
      if (static_cast<const _StructBase *>(n)->templateParams.size() !=
          st0->templateParams.size())
      {
        return renderUnion(nodes, imports, filename);
      }
    }
    auto templateArg = [&](int k) {
      return column([k](const BindingBase *n) {
        return static_cast<const _StructBase *>(n)->templateParams[k].type;
      });
    };
    if (nameIs(st0->name, "litestl::util::String") || nameIs(st0->name, "litestl::util::CString")) {
      return "string";
    }
    if (nameIs(st0->name, "litestl::util::Span")) {
      return "ArrayLike<" + render(templateArg(0), ctx, imports, filename, Pos::Member) +
             ">";
    }
    if (nameIs(st0->name, "litestl::util::Vector")) {
      if (pos == Pos::Return) {
        bool bytes = true;
        for (const BindingBase *n : templateArg(0)) {
          bytes = bytes && nameIs(n->name, "uint8");
        }
        if (bytes) {
          return "Uint8Array";
        }
      }
      return "(" + render(templateArg(0), ctx, imports, filename, Pos::Member) + ")[]";
    }

    string s = formatImport(n0, filename);
    if (s.size() > 0) {
      imports.add(s);
    }
    string out = shortName(st0->name);
    if (st0->templateParams.size() > 0) {
      out += "<";
      for (int k = 0; k < st0->templateParams.size(); k++) {
        if (k > 0) {
          out += ",";
        }
        out += render(templateArg(k), ctx, imports, filename, Pos::Member);
      }
      out += ">";
    }
    return out;
  }
  default:
    return renderUnion(nodes, imports, filename);
  }
}

/**
 * Renders a callable's generics, parameter list and return type. `entries`
 * are the descriptors rendered together (one method across a template
 * struct's instantiations, or a function template's instantiations);
 * `dispatchGroup` is what the runtime chooses among, which decides between
 * inferring the template arguments and taking a selector key.
 */
RenderedCallable TypescriptGenerator::renderCallable(const Vector<Callable> &entries,
                                                     const Vector<const Method *> &dispatchGroup,
                                                     bool keyIncludesOwner,
                                                     const string &mapName,
                                                     Set<string> &imports,
                                                     const string &filename)
{
  RenderedCallable out;
  const Callable &e0 = entries[0];
  const int ownerCount = e0.owner ? e0.owner->templateParams.size() : 0;
  const int methodCount = e0.m->templateParams.size();

  TemplCtx ctx;
  bool aligned = true;
  for (const Callable &e : entries) {
    Vector<StructTemplate> row;
    if (e.owner) {
      for (const auto &p : e.owner->templateParams) {
        row.append(p);
      }
    }
    for (const auto &p : e.m->templateParams) {
      row.append(p);
    }
    if (row.size() != ownerCount + methodCount ||
        e.m->params.size() != e0.m->params.size())
    {
      aligned = false;
    }
    ctx.rows.append(row);
  }

  // Template parameters the runtime dispatches on: the method's own, plus the
  // owner's for static methods of a template struct.
  const int firstDispatch = keyIncludesOwner ? 0 : ownerCount;
  const dispatch::Mode mode = dispatch::mode(dispatchGroup);
  const bool templated = mode != dispatch::Mode::Single &&
                         ownerCount + methodCount > firstDispatch;

  Vector<string> names;
  for (int j = 0; j < ownerCount + methodCount; j++) {
    string name = ctx.rows[0][j].name;
    // A method's own parameter can shadow its struct's (both named T).
    if (j >= ownerCount && names.contains(name)) {
      name += "_";
    }
    names.append(name);
  }

  for (int j = 0; j < ownerCount + methodCount; j++) {
    ctx.paramText.append(names[j]);
  }

  if (templated && mode == dispatch::Mode::Select) {
    out.mapDecl = "export type " + mapName + " = {\n";
    Vector<string> keys;
    for (const Callable &e : entries) {
      string key = dispatch::selectorKey(e.m, keyIncludesOwner ? e.owner : nullptr);
      if (keys.contains(key)) {
        continue;
      }
      keys.append(key);
      out.mapDecl += "  \"" + escapeString(key) + "\": [";
      bool firstElem = true;
      const Callable *src = &e;
      Vector<StructTemplate> row;
      if (src->owner) {
        for (const auto &p : src->owner->templateParams) {
          row.append(p);
        }
      }
      for (const auto &p : src->m->templateParams) {
        row.append(p);
      }
      for (int j = firstDispatch; j < row.size(); j++) {
        if (!firstElem) {
          out.mapDecl += ", ";
        }
        firstElem = false;
        out.mapDecl += formatType(row[j].type, true);
        addImport(row[j].type, imports, filename);
      }
      out.mapDecl += "],\n";
    }
    out.mapDecl += "};\n";
    for (int j = firstDispatch; j < ownerCount + methodCount; j++) {
      char buf[32];
      snprintf(buf, sizeof(buf), "%d", j - firstDispatch);
      ctx.paramText[j] = mapName + "[K][" + buf + "]";
    }
    out.generics = "<K extends keyof " + mapName + ">";
    out.params = "_t: K";
  } else if (templated) {
    out.generics = "<";
    for (int j = firstDispatch; j < ownerCount + methodCount; j++) {
      Vector<const BindingBase *> values;
      for (int i = 0; i < entries.size(); i++) {
        if (j < ctx.rows[i].size()) {
          values.append(ctx.rows[i][j].type);
        }
      }
      if (j > firstDispatch) {
        out.generics += ", ";
      }
      out.generics += names[j] + " extends " + renderUnion(values, imports, filename);
    }
    out.generics += ">";
  }

  if (!aligned) {
    ctx.rows.clear();
  }

  auto columnOf = [&](auto getter) {
    Vector<const BindingBase *> col;
    if (!aligned) {
      col.append(getter(e0.m));
      return col;
    }
    for (const Callable &e : entries) {
      col.append(getter(e.m));
    }
    return col;
  };

  for (int k = 0; k < e0.m->params.size(); k++) {
    const auto &p = e0.m->params[k];
    string pname = p.name.size() > 0 ? p.name : string("arg");
    if (p.name.size() == 0) {
      char buf[16];
      snprintf(buf, sizeof(buf), "%d", k);
      pname = pname + string(buf);
    }
    if (isReservedWord(pname)) {
      pname += "_";
    }
    if (out.params.size() > 0) {
      out.params += ", ";
    }
    out.params +=
        pname + ": " +
        render(columnOf([k](const Method *m) { return m->params[k].type; }),
               ctx,
               imports,
               filename,
               Pos::Param);
  }

  if (e0.m->returnType) {
    out.ret = render(columnOf([](const Method *m) { return m->returnType; }),
                     ctx,
                     imports,
                     filename,
                     Pos::Return);
  } else {
    out.ret = "void";
  }
  return out;
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
  string mapShortName = shortName(u->mapName);

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
  files->add(filename, string(header) + importString + "\n" + body);
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

/** Emits one file per struct template (or plain struct), rendered from all of
 * its instantiations. */
void TypescriptGenerator::buildStructFile(const StructGroup &group)
{
  const types::_StructBase *st = group[0];
  const BindingBase *type = st;
  string s = header;
  string filename = getFileName(type->name);
  string structShort = shortName(st->name);
  Set<string> imports;

  // Vector<T> / String are TS-mapped (T[] / string), not standalone classes —
  // don't register them as ClassRefs.
  for (const types::_StructBase *inst : group) {
    if (!isSpecialStruct(inst)) {
      classRefs.add({formatType(inst, false, false),
                     getModuleName(inst->name),
                     inst->buildFullName(),
                     formatTemplate(inst, classRefImports, classRefFilename, false),
                     inst->templateParams.size() > 0});
    }
  }

  // Scan members for the (at most one) `ParentTemplateParam::disambiguator`
  // back-reference. When present, we add a TS-only `K extends keyof <map>`
  // template parameter, a `[getTypeSymbol]: K` discriminator field, and
  // rewrite each tagged member's type to `<map>[K]`.
  const types::Enum *disEnum = nullptr;
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
      break;
    }
    disEnum = p->disambiguator;
  }

  const types::Union *disUnion = disEnum ? findUnionForEnum(disEnum) : nullptr;
  string mapShortName = disUnion ? shortName(disUnion->mapName) : string("");

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
    imports.add(
        "import type {" + mapShortName + "} from \"" +
        path::relative(path::dirname(filename), getModuleName(disUnion->mapName)) +
        "\";");
    imports.add("import {getTypeSymbol} from \"" + string(runtimeModule) + "\";");
  }

  TemplCtx ctx;
  for (const types::_StructBase *inst : group) {
    ctx.rows.append(inst->templateParams);
  }
  for (const auto &p : st->templateParams) {
    ctx.paramText.append(p.name);
  }
  for (const types::_StructBase *inst : group) {
    if (inst->templateParams.size() != st->templateParams.size()) {
      ctx.rows.clear();
      break;
    }
  }
  TemplCtx single;

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

    Vector<const BindingBase *> nodes;
    for (const types::_StructBase *inst : group) {
      for (auto &m2 : inst->members) {
        if (m2.name == member.name) {
          nodes.append(m2.type);
          break;
        }
      }
    }
    const bool aligned = nodes.size() == group.size();
    if (!aligned) {
      nodes.clear();
      nodes.append(member.type);
    }
    s += "  " + member.name + ": " +
         render(nodes, aligned ? ctx : single, imports, filename, Pos::Member) + "\n";
  }

  // A plain instance method is matched across instantiations by JS name and
  // occurrence (overloads keep emitting as TS overloads). Template methods and
  // static methods are grouped by JS name: the runtime dispatches among a
  // template method's instantiations within one struct instantiation (`this`
  // fixes the struct's arguments), and among a static's across all of them.
  util::Map<string, int> occurrences;
  Vector<string> doneGroups;
  string staticBody;
  string mapDecls;
  for (const Method *m : st->methods) {
    string jsName = m->getJsName();
    const bool plain = !m->isStatic && m->templateParams.size() == 0;
    Vector<Callable> entries;
    Vector<const Method *> dispatchGroup;

    if (plain) {
      int *seen = occurrences.lookup_ptr(jsName);
      if (!seen) {
        occurrences.add(jsName, 0);
        seen = occurrences.lookup_ptr(jsName);
      }
      const int k = (*seen)++;
      for (const types::_StructBase *inst : group) {
        int n = 0;
        for (const Method *m2 : inst->methods) {
          if (!m2->isStatic && m2->templateParams.size() == 0 && m2->getJsName() == jsName &&
              n++ == k)
          {
            entries.append({m2, inst});
            break;
          }
        }
      }
      if (entries.size() != group.size()) {
        entries.clear();
        entries.append({m, st});
      }
      dispatchGroup.append(m);
    } else {
      string groupKey = jsName + (m->isStatic ? "#static" : "#template");
      if (doneGroups.contains(groupKey)) {
        continue;
      }
      doneGroups.append(groupKey);
      for (const types::_StructBase *inst : group) {
        for (const Method *m2 : inst->methods) {
          if (m2->isStatic == m->isStatic && m2->getJsName() == jsName &&
              (m->isStatic || m2->templateParams.size() > 0))
          {
            entries.append({m2, inst});
            if (m->isStatic || inst == st) {
              dispatchGroup.append(m2);
            }
          }
        }
      }
    }
    string mapName = structShort + "_" + jsName + "_Args";
    RenderedCallable rc = renderCallable(
        entries, dispatchGroup, m->isStatic, mapName, imports, filename);
    mapDecls += rc.mapDecl;
    if (m->isStatic) {
      staticBody += "  " + jsName + ": bindStatic(\"" + escapeString(st->name) + "\", \"" +
                    escapeString(jsName) + "\") as " + rc.generics + "(" + rc.params +
                    ") => " + rc.ret + ",\n";
    } else {
      s += "  " + jsName + rc.generics + "(" + rc.params + "): " + rc.ret + "\n";
    }
  }

  for (int ci = 0; ci < st->constructors.size(); ci++) {
    const types::Constructor *c = st->constructors[ci];
    Vector<const types::Constructor *> ctors;
    for (const types::_StructBase *inst : group) {
      if (ci < inst->constructors.size() &&
          inst->constructors[ci]->params.size() == c->params.size())
      {
        ctors.append(inst->constructors[ci]);
      }
    }
    const bool aligned = ctors.size() == group.size();
    s += "  new(";
    for (int k = 0; k < c->params.size(); k++) {
      const auto &p = c->params[k];
      string pname = p.name.size() > 0 ? p.name : string("arg");
      if (p.name.size() == 0) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", k);
        pname = pname + string(buf);
      }
      Vector<const BindingBase *> nodes;
      if (aligned) {
        for (const types::Constructor *c2 : ctors) {
          nodes.append(c2->params[k].type);
        }
      } else {
        nodes.append(p.type);
      }
      if (k > 0) {
        s += ", ";
      }
      s += pname + ": " +
           render(nodes, aligned ? ctx : single, imports, filename, Pos::Param);
    }
    string self;
    if (!isSpecialStruct(type) && group.size() >= 2 && ctx.rows.size() > 0) {
      Vector<const BindingBase *> selfNodes;
      for (const types::_StructBase *inst : group) {
        selfNodes.append(inst);
      }
      self = render(selfNodes, ctx, imports, filename, Pos::Member);
    } else if (isSpecialStruct(type)) {
      // formatType renders special structs as their JS shape (Vector -> T[]);
      // a constructor returns the bound interface, `Vector<T>`, with the
      // declaration's defaults (`<T = any>`) dropped.
      std::string decl = templDecl.c_str(), args;
      bool skipping = false;
      for (char ch : decl) {
        if (ch == '=') {
          skipping = true;
        } else if (ch == ',' || ch == '>') {
          skipping = false;
        }
        if (!skipping) {
          args += ch;
        }
      }
      std::erase(args, ' ');
      self = formatType(type, false, false) + string(args.c_str());
    } else {
      self = formatType(type) + formatTemplate(type, imports, filename, false);
    }
    s += "): " + self + "\n";
  }
  s += "}\n";

  if (staticBody.size() > 0) {
    imports.add("import {bindStatic} from \"" + string(runtimeModule) + "\";");
    s += "\n" + mapDecls + "export const " + structShort + " = {\n" + staticBody + "};\n";
  } else if (mapDecls.size() > 0) {
    s += "\n" + mapDecls;
  }

  string importString = "";
  for (auto &import : imports) {
    importString += import + "\n";
  }
  s = importString + "\n" + s;

  files->add(filename, s);
}

/** One `functions.ts` per namespace holding free functions. */
void TypescriptGenerator::buildFunctionFiles()
{
  // Group by qualified JS name, then by namespace.
  util::Map<string, Vector<const Method *>> groups;
  Vector<string> groupNames;
  for (const Method *m : functions) {
    string key = m->qualifiedJsName();
    auto *g = groups.lookup_ptr(key);
    if (!g) {
      groups.add(key, Vector<const Method *>());
      groupNames.append(key);
      g = groups.lookup_ptr(key);
    }
    g->append(m);
  }
  std::sort(groupNames.begin(), groupNames.end(), [](const string &a, const string &b) {
    return compareNames(a, b) < 0;
  });

  util::Map<string, string> bodies;
  util::Map<string, Set<string>> fileImports;
  Vector<string> filenames;

  for (const string &key : groupNames) {
    const Vector<const Method *> &group = groups.lookup(key);
    const Method *m0 = group[0];
    string ns = m0->namespaceName();
    string filename = getFileName(ns.size() > 0 ? ns + "::functions" : string("functions"));
    if (!bodies.contains(filename)) {
      bodies.add(filename, string(""));
      fileImports.add(filename, Set<string>());
      filenames.append(filename);
    }
    Set<string> &imports = *fileImports.lookup_ptr(filename);

    Vector<Callable> entries;
    for (const Method *m : group) {
      entries.append({m, nullptr});
    }
    string jsName = m0->getJsName();
    RenderedCallable rc =
        renderCallable(entries, group, false, jsName + "_Args", imports, filename);
    string &body = *bodies.lookup_ptr(filename);
    body += rc.mapDecl;
    body += "export const " + jsName + " = bindFunction(\"" + escapeString(key) + "\") as " +
            rc.generics + "(" + rc.params + ") => " + rc.ret + ";\n";
  }

  for (const string &filename : filenames) {
    Set<string> &imports = *fileImports.lookup_ptr(filename);
    imports.add("import {bindFunction} from \"" + string(runtimeModule) + "\";");
    string importString;
    for (auto &import : imports) {
      importString += import + "\n";
    }
    files->add(filename, string(header) + importString + "\n" + bodies.lookup(filename));
  }
}

string TypescriptGenerator::buildHelpersFile()
{
  string helpers = string(header) + "\n";
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

  // Instantiations of one template share a file; render them together.
  util::Map<string, StructGroup> structGroups;
  Vector<string> structNames;
  for (auto type : typeMap.values()) {
    if (type->type != BindingType::Struct) {
      continue;
    }
    const types::_StructBase *st = static_cast<const types::_StructBase *>(type);
    if (nameIs(st->name, "litestl::util::String") || nameIs(st->name, "litestl::util::CString") ||
        nameIs(st->name, "litestl::util::Span"))
    {
      continue;
    }
    auto *g = structGroups.lookup_ptr(st->name);
    if (!g) {
      structGroups.add(st->name, StructGroup());
      structNames.append(st->name);
      g = structGroups.lookup_ptr(st->name);
    }
    g->append(st);
  }
  std::sort(structNames.begin(), structNames.end(), [](const string &a, const string &b) {
    return compareNames(a, b) < 0;
  });
  for (const string &name : structNames) {
    StructGroup &g = *structGroups.lookup_ptr(name);
    std::sort(g.begin(), g.end(), [](const types::_StructBase *a, const types::_StructBase *b) {
      return compareNames(a->buildFullName(), b->buildFullName()) < 0;
    });
    buildStructFile(g);
  }

  for (auto type : typeMap.values()) {
    if (type->type == BindingType::Union) {
      buildUnionMapFile(static_cast<const types::Union *>(type));
    }
  }

  if (functions.size() > 0) {
    buildFunctionFiles();
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
