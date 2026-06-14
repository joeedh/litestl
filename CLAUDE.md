# litestl

A self-contained foundational C++20 library used by Sculptcore. Provides
small-buffer-optimized containers, a math library, a path utility, a
platform shim, a serialization layer, and the C++ reflection / TS-binding
system used to bridge the WASM surface.

litestl is intended to be reusable in other projects — treat it as a
sub-library, not as free-form Sculptcore code. The parent
`sculptcore/CLAUDE.md` covers the higher-level build/WASM workflow; this
file covers the things that are specific to litestl.

## Debugging
in platform/platform.h:
* printStackTrace(): print stack trace to stdout
* debugBreak: break in debugger (calls __debugbreak on windows, 
  derefs a nullptr on other platforms/wasm).

## Layout

```
litestl/
  util/          containers, hash, alloc, function, task, string, span, ...
  math/          vector / matrix / quaternion / aabb / geom / math bindings
  platform/      cpu, time, export macros
  path/          path utilities (prefer over std::filesystem in engine code)
  binding/       C++ reflection + TS generator + TS runtime
    generators/      backends (currently TypeScript)
    typescriptRuntime/   TS-side counterpart of the C++ descriptors
  io/            serialization (binfile, serial)
  tests/         GTest-flavored native tests, driven by DTL
  extern/        vendored eigen (headers only)
  build_files/   shared cmake/macros + WASM helpers
  documentation/ binding.md, containers.md
```

The library is built as part of Sculptcore (`source/litestl/` of that
project), but `CMakePresets.json` here lets you configure it standalone
for IDE use (`cmake --preset debug` etc.). Standalone configures the
native build; WASM still goes through Sculptcore's `make.mjs` so the
emsdk environment is set up correctly.

## Containers and hot-path conventions

- All hot-path containers live in `util/` and the `litestl::util`
  namespace: `Vector`, `Map`, `Set`, `Span`, `Array`, `BoolVector`,
  `OrderedSet`, `BinaryHeap`, `AtomicLinkedList`, ...
- They are small-buffer-optimized (templated inline capacity), don't
  throw, and align inline storage with `ContainerAlign<T>()` so WASM's
  4-byte `void*` doesn't break 8-byte scalars.
- Prefer them over STL equivalents. See `documentation/containers.md`.
- Hash via `litestl::hash::hash(...)` in `util/hash.h`; user types can
  participate by exposing a `computeHash()` method (the `hash()` overload
  for "types with `computeHash`" in `util/hash.h` picks that up
  automatically).
- Memory: allocate through `litestl::alloc::alloc / release / New /
  Delete` — these feed the leak tracker. Use
  `alloc::PermanentGuard` around one-off bootstrap allocations (e.g.
  `defineBindings()` bodies) to mark them as expected permanent.

## Math

- `math/vector.h`, `math/matrix.h`, `math/quat.h`, `math/aabb.h`,
  `math/geom.h`, plus `math_bindings.h` which registers the math types
  with the binding system.
- Eigen is vendored under `extern/eigen` and exposed as the `eigen`
  CMake target (header-only). Pull it in only when you need a solver or
  decomposition — the litestl math types cover the vector/matrix
  arithmetic used in hot code.

## Path

`path/path.h` exposes `litestl::path` utilities. Use them rather than
ad-hoc string manipulation or raw `std::filesystem` in engine code —
they're WASM-safe and consistent across platforms.

## Binding system (`binding/`)

C++ reflection / type-description layer; see
`documentation/binding.md` for the full design. Quick reminders:

- Opt a class in by exposing
  `static const types::Struct<Self> *defineBindings()`.
- Register members with `BIND_STRUCT_MEMBER(def, field)`, methods with
  `BIND_STRUCT_METHOD(def, name, MARGS(...))` (or
  `BIND_STRUCT_METHOD_SIG` for overloaded methods), and constructors
  with `BIND_STRUCT_DEFAULT_CONSTRUCTOR`,
  `BIND_STRUCT_CONSTRUCTOR(def, name, Args...)`, and
  `BIND_STRUCT_COPY_CONSTRUCTOR(st)`.
- A struct that will cross the WASM boundary *by value* needs a
  registered `"copy"` constructor — the TS runtime uses it when packing
  arguments.
- The system intentionally allocates descriptors with `new` and never
  deletes them. This is scaffolding for an eventual `constexpr` /
  `consteval` redesign; do not refactor it to fix the "leaks" or to
  templatize storage unless that's explicitly the task. Compile-time
  dispatch through `Binder<T>` specializations (explicit or
  concept-constrained, e.g. `ClassBindingReq`) is the part that should stay.
- The TS-side runtime lives in `binding/typescriptRuntime/`. It reads
  descriptors out of the WASM heap using the `BindingInfo` offsets table
  populated in `binding.cc::LSTL_GetBindingInfo`. If you add fields to a
  descriptor, update both that table *and* the matching TS class in
  `typescriptRuntime/binding.ts`.

## Tests

- Native only — gated on `BUILD_WASM=OFF` in the parent build, or built
  directly with one of the CMake presets here.
- Tests use a small custom harness (`test_init`, `test_assert`,
  `test_end`) plus DTL (a diff utility, fetched via `FetchContent`).
- File naming: `test_<thing>.cc` under `tests/`. Wire each new test
  through `tests/CMakeLists.txt` with the `test(<file>.cc, <extra_lib>)`
  macro.
- `test_binding_system.cc` doubles as a WASM-side harness when
  `BUILD_WASM=ON`; the linker config there sets a 900MB initial memory
  and allows growth.

## Style

- C++20, headers `.h`, sources `.cc` (`.cpp` is accepted but `.cc` is the
  convention).
- `-Wno-invalid-offsetof` is set under WASM — `offsetof` on non-standard-
  layout types is expected and is how the binding system threads
  descriptors across the WASM boundary.
- Keep comments minimal; explain *why*, not *what*. Identifier names
  carry the load.
- No backward-compat shims when renaming internals — this is a
  single-repo project.
- Don't add a backing header for every helper; module granularity here
  is already fairly fine, prefer editing existing files.
