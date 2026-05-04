# The `litestl::binding` System

<!-- toc -->

- [Overview](#overview)
- [The core idea: `Bind()`](#the-core-idea-bind)
- [Describing a struct](#describing-a-struct)
- [Collecting and emitting](#collecting-and-emitting)
- [Design trajectory](#design-trajectory)
<!-- auto-generated with markdown-toc! regenerate with ${SCRIPT_NAME} -->

<!-- tocstop -->

## Overview

`litestl::binding` is a lightweight C++ reflection layer that lets you describe
C++ types — primitives, arrays, and user-defined structs — in a form that can
be walked at runtime and emitted to other languages. Its first consumer is a
TypeScript interface generator (`generators/typescript.cc`), but the descriptor
model is language-agnostic and designed to be extended with additional backends
(JSON schema, Python, bindings glue, etc.).

The header layout mirrors the conceptual layering:

| Header | Role |
| --- | --- |
| `binding_base.h` | `BindingBase`, the polymorphic root, plus the `BindingType` and `NumberType` tag enums. |
| `binding_types.h` | Primitive and indirection descriptors: `types::Boolean`, `types::Number<T>`, `types::Array<T>`, `types::Pointer`, `types::Reference`. |
| `binding_struct.h` | `types::Struct<CLS>`, the `ClassBindingReq` concept, the `BIND_STRUCT_MEMBER` macro, and the `Method` / `Constructor` descriptors attached to a struct. |
| `binding_literal.h` | `types::LiteralType` and the `NumLitType` / `BoolLitType` / `StrLitType` literal descriptors used as template arguments. |
| `binding_utils.h` | `Bind<T>()` overloads for the built-in numeric types. |
| `manager.h` | `BindingManager`, a name → descriptor registry (C++ side). |
| `generators/typescript.h` | Emitter that turns a set of descriptors into `.ts` files, including a generated index re-exporting all bound classes and an `AllBoundTypes` helper type. |
| `runtime/manager.ts` | TypeScript-side runtime registry; provides the strongly-typed `construct<K extends keyof AllBoundTypes>()` factory that consumes the generated index. |
| `binding.h` | Umbrella header that pulls the C++ pieces together. |

## The core idea: `Bind<T>()`

Every type that participates in the binding system is reachable through a
single generic function:

```cpp
template <typename T> const BindingBase *Bind();
```

`Bind<T>()` returns a pointer to the most-derived `BindingBase` subclass that describes `T`.
The correct overload is selected at **compile time** using C++20 concepts — the
binding system never inspects a runtime type tag to decide how to describe a
C++ type. Which overload fires depends on what `T` is:

- For a **built-in numeric type** (`int`, `uint64_t`, `short`, …), a
  `std::same_as<ctype>`-constrained overload in `binding_utils.h` produces a
  `types::Number<T>`.
- For a **user-defined class** that opts in via a static `defineBindings()`
  method, the `ClassBindingReq` concept in `binding_struct.h` selects an
  overload that returns the class's own `types::Struct<CLS>` descriptor.

If no overload matches, compilation fails cleanly — a type that cannot be
described is caught the moment someone tries to bind it, not at runtime.

### Use of new
Binding types are all created using new() instead of the litestl allocator, 
and are currently never deleted.  Since they are only created once this 
isn't a problem.

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

Note: bindings always describe a *concrete* type — the system cannot bind a
raw class template, only a specific instantiation of one (because it cannot
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

References to MyTemplate would include the literal parameters, e.g.
```ts
export class MyClass {
  templateInstance: MyTemplate<number, 42>;
}
```

The TypeScript generator additionally emits an index module that re-exports
every bound class and exposes an `AllBoundTypes` helper type. Passing it to
the TS-side `BindingManager` (`runtime/manager.ts`) gives you a strongly
typed `construct` factory keyed by the fully qualified name:

```ts
const instance = bindingManager.construct('MyTemplate<number, 42>');
```

...assuming that particular instantiation was registered with the manager.
Note that `construct` and `AllBoundTypes` live on the TypeScript side; the
C++ `BindingManager` (`manager.h`) is a plain registry and does not
construct objects itself.

## Collecting and emitting

`BindingManager` (in `manager.h`) is a simple registry keyed by type name. Adding
a struct to it recursively walks the member list and registers each referenced
descriptor, so a single `add()` call pulls in a whole type graph.

The TypeScript generator takes a `Vector<const BindingBase *>` of root types,
walks their members transitively, and produces one file per struct — each with
an `export interface` declaration and the necessary cross-file `import`
statements, mapping C++ `::` namespace separators to `/` path segments.

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
