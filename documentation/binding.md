# The `litestl::binding` System

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
| `binding_types.h` | Primitive descriptors: `types::Boolean`, `types::Number<T>`, `types::Array<T>`. |
| `binding_struct.h` | `types::Struct<CLS>`, the `ClassBindingReq` concept, and the `BIND_STRUCT_MEMBER` macro. |
| `binding_utils.h` | `Bind<T>()` overloads for the built-in numeric types. |
| `manager.h` | `BindingManager`, a name → descriptor registry. |
| `generators/typescript.h` | Emitter that turns a set of descriptors into `.ts` files. |
| `binding.h` | Umbrella header that pulls the above together. |

## The core idea: `Bind<T>()`

Every type that participates in the binding system is reachable through a
single generic function:

```cpp
template <typename T> /* descriptor */ Bind();
```

`Bind<T>()` returns a pointer to a `BindingBase` subclass that describes `T`.
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

## Describing a struct

A class opts in by exposing a static `defineBindings()` method that returns a
`types::Struct<Self>*`. Inside that method, fields are registered via the
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
