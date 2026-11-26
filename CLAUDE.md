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
* getStackTrace(): return the current stack trace as a std::string
* debugBreak: break in debugger (calls __debugbreak on windows, 
  derefs a nullptr on other platforms/wasm).

## Layout

```
litestl/
  util/          containers, hash, alloc, function, task, string, span, ...
  math/          vector / matrix / quaternion / aabb / geom / math bindings,
                 wideint (Int128/Int256), predicates (+ predicates/ sources)
  platform/      cpu, time, export macros
  path/          path utilities (prefer over std::filesystem in engine code)
  binding/       C++ reflection + TS generator + TS runtime
    generators/      backends (currently TypeScript)
    typescriptRuntime/   TS-side counterpart of the C++ descriptors
    napi/            N-API runtime (target `binding_napi`)
  io/            serialization (binfile, serial), bytes.h (header-only byte stream)
  attr/          paged copy-on-write attribute columns and groups (target `attr`)
  tests/         GTest-flavored native tests, driven by DTL
    bindings/        binding fixture + WASM/N-API conformance suite (js/)
  extern/        vendored eigen (headers only)
  build_files/   shared cmake/macros + WASM helpers
  documentation/ binding.md, containers.md, numerics.md, attributes.md
```

The library is built as part of Sculptcore (`source/litestl/` of that
project), and also builds standalone:

- `node configureEnv.mjs --fetch-deps` downloads the wasi sysroot,
  compiler-rt and node-api headers into `build/deps` (`LITESTL_DEPS_DIR`).
- `node configureEnv.mjs <cmd>` runs `<cmd>` inside the vcvars64
  environment on Windows; elsewhere it runs `<cmd>` unchanged.
- Presets (`build/<preset>/`): `msvc` and `vs-clang` (VS's clang-cl) on
  Windows; `gcc` and `clang` elsewhere; `wasm` (stock clang + wasi
  sysroot, no Emscripten, `build_files/wasm-clang.cmake`).
- Every clang gets `-fno-delayed-template-parsing`, so clang-cl rejects
  what gcc rejects. Write `def->template foo<...>` in dependent contexts.
- Sculptcore's WASM build still uses Emscripten via its `make.mjs`; keep
  `__EMSCRIPTEN__` paths (util/wasm.h, util/task.h) working.

## Containers and hot-path conventions

- All hot-path containers live in `util/` and the `litestl::util`
  namespace: `Vector`, `Map`, `Set`, `Span`, `Array`, `BoolVector`,
  `OrderedSet`, `BinaryHeap`, `AtomicLinkedList`, ...
- They are small-buffer-optimized (templated inline capacity), don't
  throw, and align inline storage with `ContainerAlign<T>()` so WASM's
  4-byte `void*` doesn't break 8-byte scalars.
- **Never use standard containers in litestl**, in library code or in tests:
  no `std::vector`, `std::map`, `std::set`, `std::unordered_map`,
  `std::unordered_set`, `std::deque` or `std::list`. Use the `util::`
  containers. They allocate through `litestl::alloc`, so the leak tracker
  sees them. See `documentation/containers.md`.
  - Exception: narrow cases that must not allocate through `litestl::alloc`
    (for example, inside the allocator or the leak tracker). Say why in a
    comment at the use site.
  - Exception: standard types that litestl re-exports in its headers, such as
    `std::span` (`util::span`), `std::array` (`util::Array`) and
    `std::function` (`util::function`). Spell them by their `util::` name.
  - Some older code and tests still use standard containers. Don't copy them.
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
- Randomized suites: include `test_harness.h` (`test::cases(n)` scales with
  `LITESTL_TEST_SCALE`; `test::FailureLog` counts failures in bulk loops and
  asserts once). Generate inputs with `splitmix64.h`, not `litestl::rng`, and
  check against `bigint_oracle.h` where an exact answer is needed. Register
  heavy suites with `test_long(...)` (ctest label `long`).
- `LITESTL_TEST_NO_BREAK=1` reports all failures instead of trapping on the
  first; `LITESTL_PRINT_GOLDENS=1` prints pinned golden values.
- Golden values (hash128 IDs, rng streams, `test_determinism_digest`) are part
  of the determinism contract: change them only deliberately, in the same
  commit as the code change that requires it.
- Numerics conventions (predicate domain, FP flags, hash encoding, rng layout):
  see `documentation/numerics.md`.
- `LITESTL_TEST_SCALE_DIV` divides case counts (sanitizer builds use 4).
  Sanitizer runs use `tests/lsan.supp` (wired via ctest) for intentional
  process-lifetime allocations.
- Attribute columns: declare builtin attributes with `attr::AttrDef` /
  `attr::Schema` / `attr::TypedGroup` (`attr/schema.h`) rather than raw slot
  indices. Compile-time errors are tested by `tests/compile_fail/` snippets
  (`compile_fail(name regex)` in tests/CMakeLists.txt).
- Value pools (`attr/pool_store.h`): `PoolRef` values don't keep entries alive;
  owners pass every ref holder as a root to `collect`/`canonicalize` (see
  `documentation/attributes.md`, "Value pools"). `test_attr_pool_model` is the
  oracle.
- Attribute columns: see `documentation/attributes.md` for the copy-on-write,
  reference-invalidation and threading rules. `test_attr_model` is the
  model-based oracle; extend it when adding column behaviour.
- Binding backends: `tests/bindings/fixture.h` is the one fixture for the
  generator test (`test_binding_functions`), the WASM module and the N-API
  addon. The ctest `binding_conformance` runs `tests/bindings/js` against
  the addon, and also against the WASM module when
  `LITESTL_CONFORMANCE_WASM` is set. It needs `npm ci` in
  `tests/bindings/js`. Extend the fixture and `conformance.test.ts`
  together; both backends must pass identical expectations.
- `test_binding_system.cc` doubles as a WASM-side harness when
  `BUILD_WASM=ON`; the linker config there sets a 900MB initial memory
  and allows growth.

## Style

- C++20, headers `.h`, sources `.cc` (`.cpp` is accepted but `.cc` is the
  convention).
- Include litestl's own files, Eigen and lz4 by a path relative to the
  including file (`"vector.h"`, `"../util/vector.h"`), never through an
  include directory, so a host needs only litestl's parent on its path.
  The `litestl_header_check` target (tests/CMakeLists.txt) compiles each
  header that way.
- `-Wno-invalid-offsetof` is set under WASM — `offsetof` on non-standard-
  layout types is expected and is how the binding system threads
  descriptors across the WASM boundary.
- Keep comments minimal; explain *why*, not *what*. Identifier names
  carry the load.
- No backward-compat shims when renaming internals — this is a
  single-repo project.
- Don't add a backing header for every helper; module granularity here
  is already fairly fine, prefer editing existing files.
