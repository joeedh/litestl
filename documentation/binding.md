# The `litestl::binding` System

<!-- toc -->

- [Overview](#overview)
- [The core idea: `Bind<T>()`](#the-core-idea-bindt)
- [Describing a struct](#describing-a-struct)
- [Methods](#methods)
- [Constructors](#constructors)
- [Enums](#enums)
- [Discriminated unions](#discriminated-unions)
- [Built-in helpers: `Vector<T>` and `string`](#built-in-helpers-vectort-and-string)
- [Collecting and emitting](#collecting-and-emitting)
- [The C API and TypeScript runtime](#the-c-api-and-typescript-runtime)
- [Design trajectory](#design-trajectory)
<!-- auto-generated with markdown-toc! regenerate with ${SCRIPT_NAME} -->

<!-- tocstop -->

## Overview

`litestl::binding` is a lightweight C++ reflection layer that lets you describe
C++ types — primitives, arrays, pointers, references, user-defined structs,
enums, and discriminated unions — in a form that can be walked at runtime and
emitted to other languages. Its first consumer is a TypeScript interface
generator (`generators/typescript.cc`) paired with a TypeScript-side runtime
(`typescriptRuntime/`), but the descriptor model is language-agnostic and
designed to be extended with additional backends (JSON schema, Python, etc.).

The header layout mirrors the conceptual layering:

| Header | Role |
| --- | --- |
| `binding_base.h` | `BindingBase`, the polymorphic root, plus the `BindingType` and `NumberType` / `NumberFlags` tag enums. |
| `binding_bind.h` | The `Binder<T>` customization point (undefined primary template) and the `Bind<T>()` dispatcher that routes to `Binder<T>::bind()`. |
| `binding_types.h` | Primitive and indirection descriptors: `types::Boolean`, `types::Number<T>`, `types::Array<T>`, `types::Pointer`, `types::Reference`. |
| `binding_number.h` | `Binder<T>` specializations for the built-in numeric C types (`char`, `short`, `int`, `int64_t`, `float`, `double`, plus unsigned variants). |
| `binding_utils.h` | `Binder<T>` specializations for `bool`, raw pointers, and references. |
| `binding_struct.h` | `types::Struct<CLS>`, the `ClassBindingReq` concept, the `BIND_STRUCT_MEMBER` macro, and the `StructMember` / `StructTemplate` records. |
| `binding_method.h` | `types::Method`, the `MethodBuilder` traits class, and the `BIND_STRUCT_METHOD` / `BIND_STRUCT_METHOD_SIG` macros. |
| `binding_constructor.h` | `types::Constructor` and `_StructBase::findConstructor`. |
| `binding_constructor_builder.h` | `ConstructorBuilder` plus the `BIND_STRUCT_CONSTRUCTOR`, `BIND_STRUCT_DEFAULT_CONSTRUCTOR`, and `BIND_STRUCT_COPY_CONSTRUCTOR` macros. |
| `binding_enum.h` | `types::Enum` with `EnumItem` and an `isBitMask` flag. |
| `binding_union.h` | `types::Union` — a TypeScript-style discriminated union (a property-name disambiguator plus a list of `UnionPair`s), not a C-style union. |
| `binding_template.h` | `types::ParentTemplateParam`, used to record a template-parameter reference that was inherited from an enclosing template. |
| `binding_literal.h` | `types::LiteralType` and the `NumLitType` / `BoolLitType` / `StrLitType` literal descriptors used as template arguments. |
| `binding.h` | Umbrella header that pulls the C++ pieces together and registers the `Vector<T>` and `string` bindings. |
| `manager.h` | `BindingManager`, a name → descriptor registry (C++ side); seeded with the built-in numerics and common `Vector<T>` specializations. |
| `binding.cc` | C-ABI surface (`LSTL_*` functions) used by WASM consumers, plus the offset/size table consumed by the TS runtime. |
| `generators/typescript.h` / `.cc` | Emitter that turns a set of descriptors into `.ts` files, including a generated index re-exporting all bound classes and an `AllBoundTypes` helper type. |
| `typescriptRuntime/` | TypeScript-side runtime registry + per-binding reader classes (`BindingManager`, `StructType`, `MethodType`, `ConstructorType`, `BoundVector`, …). |

## The core idea: `Bind<T>()`

Every type that participates in the binding system is reachable through a
single generic function (`binding_bind.h`):

```cpp
template <typename T> struct Binder;          // undefined primary

template <typename T> auto Bind()
{
  return Binder<T>::bind();
}
```

`Bind<T>()` returns a pointer to the most-derived `BindingBase` subclass that
describes `T`. The dispatch goes through the `Binder<T>` **class-template
customization point**: each bindable type provides a specialization with a
`static bind()` member, selected at **compile time** (explicit specializations
and C++20-constrained partial specializations) — the binding system never
inspects a runtime type tag to decide how to describe a C++ type. Because
`Binder<T>::bind` is a dependent qualified name, a specialization only needs
to be declared before the first `Bind<T>()` call that a TU instantiates; this
is what lets downstream modules (mesh, gpu, props, …) register their own types
under conforming two-phase name lookup. Which specialization fires depends on
what `T` is:

- For a **built-in numeric type** (`int`, `uint64_t`, `short`, …), an explicit
  specialization in `binding_number.h` produces a `types::Number<T>`. Unsigned
  variants get the `NumberFlags::Unsigned` flag set and their TS-side name
  prefixed with `u` (so `unsigned int` → `"uint32"`).
- For `bool`, a `types::Boolean`.
- For a **raw pointer** `T*`, a partial specialization produces a
  `types::Pointer` wrapping `Bind<T>()`. The special case `void *` (an explicit
  specialization, which always beats the partial one) produces an
  unsigned-integer descriptor sized for the target pointer width.
- For a **reference** `T&`, a `types::Reference`.
- For a **user-defined class** that opts in via a static `defineBindings()`
  method, the `ClassBindingReq`-constrained partial specialization in
  `binding_struct.h` returns the class's own `types::Struct<CLS>` descriptor.
- For **`litestl::util::Vector<T, N>`** and **`litestl::util::string`**, the
  specializations in `binding.h` produce a `types::Struct` describing the
  container.

If no specialization matches, compilation fails cleanly (the primary template
is undefined) — a type that cannot be described is caught the moment someone
tries to bind it, not at runtime.

### Use of `new`

Binding types are all created using `new` instead of the litestl allocator,
and are currently never deleted. Since they are only created once (descriptors
are statically cached inside `defineBindings()`) this isn't a problem; tests
wrap their `defineBindings()` body in an `alloc::PermanentGuard` so the leak
tracker treats these allocations as permanent.

## Describing a struct

A class opts in by exposing a static `defineBindings()` method that returns a
`const types::Struct<Self>*`. Inside that method, fields are registered via the
`BIND_STRUCT_MEMBER` macro, which captures the field's name, its `offsetof`,
and recursively calls `Bind<FieldType>()` for the field's type:

```cpp
struct Vec3 {
  float x, y, z;

  static const types::Struct<Vec3> *defineBindings() {
    static auto *def = new types::Struct<Vec3>("Vec3", sizeof(Vec3));
    BIND_STRUCT_MEMBER(def, x);
    BIND_STRUCT_MEMBER(def, y);
    BIND_STRUCT_MEMBER(def, z);
    return def;
  }
};
```

Because `BIND_STRUCT_MEMBER` dispatches through `Bind<T>()`, nested structs
compose naturally: a `Transform` with a `Vec3 position` field just works, as
long as `Vec3` itself provides `defineBindings()`.

`types::Struct<CLS>` also captures a `DestructorThunk` that wraps `~CLS()`, so
the TS runtime can destroy WASM-side instances generically. `Struct::inherit`
copies members and methods from a parent `_StructBase` to model inheritance.
`Struct::setNonNull("memberName")` clones a pointer member's descriptor and
flips its `isNonNull` flag, which the TS generator emits as a non-`null` type.

### Template instantiations

Bindings always describe a *concrete* type — the system cannot bind a raw
class template, only a specific instantiation of one (because it cannot
synthesize code for type parameters it has never seen). It does, however,
retain the template arguments of each instantiation as part of the descriptor,
so the generator can reproduce them in the target language. For example:

```c++
template<typename T, int staticValue>
class MyTemplate {
public:
  T value;

  static const types::Struct<MyTemplate<T, staticValue>> *defineBindings() {
    static auto *def = new types::Struct<MyTemplate<T, staticValue>>(
        "MyTemplate", sizeof(MyTemplate<T, staticValue>));
    def->addTemplateParam(Bind<T>(), "T");
    def->addTemplateParam(
        new types::NumLitType(staticValue, "staticValue", Bind<int>()),
        "staticValue");
    BIND_STRUCT_MEMBER(def, value);
    return def;
  }
};
```

If you registered an instantiation such as `MyTemplate<int, 42>`, you might
get the following TypeScript code:

```ts
export interface MyTemplate<T, staticValue extends number> {
  value: T;
}
```

References to `MyTemplate` would include the literal parameters, e.g.

```ts
export class MyClass {
  templateInstance: MyTemplate<number, 42>;
}
```

When a nested struct references one of its enclosing template's parameters
(rather than a concrete type), the binding records that with a
`types::ParentTemplateParam`. It carries the original parameter name, a
parent-depth index (how many levels up the parameter was declared), and the
resolved concrete type for the current instantiation. The TS generator emits
the parameter name; the runtime can still recover the concrete type via
`ParentTemplateParam::concreteType` when it needs sizes/layouts.

## Methods

`types::Method` describes a member function: its name, return type, parameter
list (`MethodParam{name, type}`), a `MethodThunk` of signature
`void(void *self, void **args, void *ret)`, and `isConst` / `isStatic` flags.
The thunk takes type-erased pointers for `this`, an array of pointers to each
argument, and a pointer to a result slot.

You almost never write a thunk by hand. `MethodBuilder<auto Mfp>` derives the
return type, parameter types, and a thunk from a member-function-pointer
template argument using `MethodTraits`. The `BIND_STRUCT_METHOD` macro wraps
that up:

```cpp
struct Foo {
  int counter = 0;
  int add(int a, int b) { counter++; return a + b; }
  int sum() const       { return counter; }
  void bump(int n)      { counter += n; }

  static types::Struct<Foo> *defineBindings() {
    alloc::PermanentGuard guard;
    auto *st = new types::Struct<Foo>("Foo", sizeof(Foo));
    BIND_STRUCT_METHOD(st, add,  MARGS("a", "b"));
    BIND_STRUCT_METHOD(st, sum,  MARGS());
    BIND_STRUCT_METHOD(st, bump, MARGS("n"));
    return st;
  }
};
```

`MARGS(...)` builds a small `Vector<const char *>` of argument names that is
applied via `Method::setArgNames` after the method is registered. If the names
list size doesn't match the parameter count the program aborts — argument
names are a contract, not a hint.

`BIND_STRUCT_METHOD_SIG(def, mname, RET, ARGNAMES, ARGS)` is the overload-aware
variant: it lets you disambiguate an overload set by writing out the return
type and parenthesised argument list, then calls into the same `MethodBuilder`
pipeline.

`Method::isNeverNull()` and `Method::argIsNullable("argName")` post-edit the
descriptor to flip pointer-nullability flags on the return type or a named
argument. By default `MethodBuilder::fillParams` sets every pointer argument
to non-null; use `argIsNullable` to relax that.

## Constructors

`types::Constructor` mirrors `types::Method` but for object construction: it
has an `ownerType`, a `params` vector, and a `ConstructorThunk` of signature
`void(void *outBuf, void **args)` that placement-new-constructs an instance
into `outBuf`. `_StructBase::findConstructor(name)` looks up a constructor by
name (e.g. `"default"`, `"main"`, `"copy"`).

Three macros cover the common cases:

```cpp
BIND_STRUCT_DEFAULT_CONSTRUCTOR(st);              // name: "default", no args
BIND_STRUCT_CONSTRUCTOR(st, "main", int, int);    // user-named, typed args
BIND_STRUCT_COPY_CONSTRUCTOR(st);                 // name: "copy", arg: T&
```

`BIND_STRUCT_CONSTRUCTOR` runs the argument types through
`ConstructorBuilder`, which calls `Bind<Arg>()` for each, then placement-news
the object inside the generated thunk. Pointer parameters are marked non-null
by default, identically to `MethodBuilder`. `Constructor::argIsNullable` can
relax that per-argument.

`BIND_STRUCT_COPY_CONSTRUCTOR` is a free function (not a macro) — it builds a
`Reference` to the owning struct and synthesises a thunk that calls `T`'s copy
constructor. The TS runtime relies on the `"copy"` constructor when passing
struct values across the WASM boundary, so any struct you intend to pass *by
value* should register one.

## Enums

`types::Enum` describes a C++ `enum` / `enum class`: a base size in bytes, an
`isBitMask` flag, and a `Vector<EnumItem>` of `{name, value}` pairs. Items are
appended with `addItem(name, value)`. The generator emits a TS `enum` (or a
union of numeric literals for bitmask enums).

To bind a new enum, specialize `Binder` for it (in a header next to the enum,
reopening `namespace litestl::binding`, so every TU agrees on the
specialization; the `bind()` body may also live out-of-line in a `.cc`):

```cpp
namespace litestl::binding {
template <> struct Binder<myns::MyEnum> {
  static const types::Enum *bind()
  {
    types::Enum *e = new types::Enum("myns::MyEnum", sizeof(myns::MyEnum));
    e->addItem("A", int(myns::MyEnum::A));
    e->addItem("B", int(myns::MyEnum::B));
    return e;
  }
};
} // namespace litestl::binding
```

## Discriminated unions

`types::Union` describes a TypeScript-style tagged union: a `disPropName`
(the discriminator property on each variant struct), a `disPropType` (its
descriptor — typically a `Number`, `Enum`, or `Boolean`), and a list of
`UnionPair{name, type, typeValue[8]}` entries. Each pair stores its discrete
discriminator value inline (in a fixed 8-byte buffer so different `T`s don't
change the layout). Add a variant with:

```cpp
Union u("kind", Bind<SomeEnum>());
u.add("Sphere",   SomeEnum::Sphere,   sphereStruct);
u.add("Cylinder", SomeEnum::Cylinder, cylinderStruct);
```

The TS runtime reads `typeValue` back through `disPropType` (handling
`Boolean`, `Number`, `Enum`, and `litestl::util::string` discriminators) and
turns each variant into a TypeScript intersection of the variant interface
with `{[disPropName]: <literal>}`.

## Built-in helpers: `Vector<T>` and `string`

`binding.h` ships two non-trivial built-in bindings:

- **`litestl::util::Vector<T, N>`** — recognised by the `IsVector` concept
  (every litestl vector exports `is_litestl_vector = std::true_type`). The
  generated `Struct` registers a default constructor, a `(T*, int)` "ptrWithCount"
  constructor, hand-rolled `resize` / `resize_no_construct_destruct` methods,
  and two template params: `T` (the element type, with pointers forced to
  non-null) and `static_size` (a `NumLitType` carrying the inline capacity).
  This is what the TS-side `BoundVector` and `findVectorClass` consume.
- **`litestl::util::string`** — registered as an opaque `Struct<string>` with
  no members; the TS runtime reads it via `readLiteStlString` using
  layout-specific offsets.

`BindingManager`'s constructor pre-registers the numeric primitives,
`void *`, and `Vector<T>` for each of those primitives, so most TS-side code
can request `findVectorClass<'int32'>(...)` without the caller having to bind
the vector explicitly.

## Collecting and emitting

`BindingManager` (in `manager.h`) is a name-keyed registry. `add(binding)`
recursively walks the type:

- For a `Struct`, it walks every member, every method's return type and
  parameter list, and every constructor's parameter list.
- For `Array` / `Pointer` / `Reference`, it walks the wrapped type.
- For `Union`, it walks the discriminator type and every variant struct.

So a single `add()` call on a root struct pulls in its whole reachable type
graph. Pure-primitive descriptors (`Boolean`, `Number`, `Method`,
`Constructor`, `Literal`, `ParentTemplParam`) are leaves and add nothing
further.

The TypeScript generator takes a `Vector<const BindingBase *>` of root types,
walks their members transitively, and produces one file per struct — each with
an `export interface` declaration and the necessary cross-file `import`
statements, mapping C++ `::` namespace separators to `/` path segments. It
also emits an index module that re-exports every bound class and exposes an
`AllBoundTypes` helper type. Passing that type to the TS-side `BindingManager`
gives you a strongly typed `construct` factory keyed by the fully qualified
name:

```ts
const instance = bindingManager.construct('MyTemplate<number, 42>');
```

…assuming that particular instantiation was registered with the manager. Note
that `construct` and `AllBoundTypes` live on the TypeScript side; the C++
`BindingManager` is a plain registry and does not construct objects itself.

## The C API and TypeScript runtime

`binding.cc` exposes a small C-ABI surface (`LSTL_*` symbols) so that the TS
runtime can walk descriptors over the WASM boundary without needing layout
knowledge of C++ classes:

- `LSTL_GetBindingInfo` returns a `BindingInfo` struct of `offsetof` /
  `sizeof` values for every descriptor type. The TS runtime caches this once
  at startup and uses the offsets to decode descriptors directly out of the
  WASM heap.
- `LSTL_Binding_GetKeys` / `LSTL_Binding_Get` enumerate the manager's
  registry by name (full names including template arguments).
- `LSTL_Struct_GetMethod*` / `LSTL_Method_GetParam*` / `LSTL_Method_Invoke`
  walk and call methods generically. `LSTL_Constructor_*` and
  `LSTL_Destructor_Invoke` do the same for constructors and the
  `destructorThunk`.
- `LSTL_GenerateTypescript` runs the generator over the entire manager and
  returns a length-prefixed pair-stream of `(filename, contents)` blobs.
- `LSTL_Binding_GetFullName` caches and returns the full templated name
  computed by `BindingBase::buildFullName` (also exposed via the `getFullName`
  member, which fills the `cachedFullName` field on first call).

The TypeScript runtime under `typescriptRuntime/` mirrors the C++ descriptor
hierarchy: `BindingBase`, `StructType`, `MethodType`, `ConstructorType`,
`PointerType`, `EnumType`, `UnionType`, `ParentTemplateParamType`, etc.
`BindingManager` (TS) wraps a WASM module, lazily wraps descriptors as it
encounters them, and provides `construct<K extends keyof AllBoundTypes>()`,
`findVectorClass<K>(elementName, staticSize)`, and the
`BoundVector` / `BoundArray` helpers for working with `litestl::util::Vector`
and fixed-size arrays from JS.

## Design trajectory

The current implementation uses runtime allocation: `Bind<T>()` returns objects
created with `new`, struct members live in a `util::Vector`, and the manager
stores descriptors in a `util::Map`. This is **intentional scaffolding**. The
long-term goal is a fully `constexpr` / `consteval` system in which the entire
descriptor graph is a compile-time constant — no allocation, no virtual
dispatch at runtime, and emitters driven purely by template metaprogramming.

Concepts already do the dispatch at compile time; the remaining work is to
move descriptor *storage* to compile-time containers (e.g. `std::array` sized
by a template parameter) and mark the relevant functions `constexpr`. Until
then, the runtime representation keeps the API malleable while the shape of
the system settles.
