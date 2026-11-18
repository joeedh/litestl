# Attribute columns (`attr/`)

`litestl::attr` is a generic, open, columnar attribute store. Columns live in pages that
are **copied on write**, so copying a column or a group gives a cheap immutable
**snapshot**. Snapshots are what procedural caches, undo and concurrent readers build
on.

| Header | What |
|---|---|
| `attr/paged_store.h` | `PagedStore<T>`: pages, copy-on-write, tails, version stamps, `ColumnValue` |
| `attr/value_ops.h` | `ValueOps<T>`: mix behaviour per value family |
| `attr/mix.h` | `MixPolicy`, `MixBatch`, `ValidatedMixBatch`, `MixContext` |
| `attr/column_base.h` | `ColumnBase` (the open interface), `ColumnFlags`, `ColumnType`, `ReadLimits` |
| `attr/column.h` | `Column<T, Ops>`: typed column, canonical hash and serialisation |
| `attr/bool_column.h` | `BoolColumn` |
| `attr/registry.h` | `ColumnTypeRegistry`: type key → column class |
| `attr/type_key.h` | Stable value type keys (`f32`, `f64x3`, `h128`, `f32[8]`, …) |
| `attr/group.h` | `AttrGroup`: named, equally sized columns with builtin slots |

The CMake target is `attr`, which depends on `util` and `math`. It is not part of the
WASM library list.

## Values

`Column<T>` stores `ColumnValue` types. These must be:
- trivially copyable and default-constructible;
- **padding-free**: arithmetic types, `Vec<E,N>`, `std::array<E,N>`, `Hash128`, or a user
  type with `is_padding_free_custom<T>` specialised.

Equality is **bitwise** everywhere (uniform-page detection, `set()` shortcuts,
serialisation runs), so −0.0 ≠ +0.0 and identical NaN payloads compare equal. The
canonical encoding of a value is its little-endian object representation.

## Pages

- **Page size.** Each store has a runtime page shift (default 12, i.e. 4096 elements),
  chosen by benchmark. Hashes and serialised bytes never depend on it.
- **Page states:**
  - **Absent:** every element equals the column default, which is fixed at
    construction.
  - **Constant:** one stored value.
  - **Dense:** an `RcBuffer` (atomic refcount) shared between snapshots.
- **Partial-capacity tail.** The last page's buffer is a power of two ≥ its live count,
  with a minimum of 16 elements (capped at the page size), so small columns stay
  small.
- **Resize:**
  - **Grow:** newly exposed elements read as the default. A stale tail is reset, after
    copying the page if it is shared.
  - **Shrink:** nothing is written.
- **Copy-on-write.** Any write first makes the page a unique Dense buffer. Reads never
  allocate.

## Reference and threading rules

- **Invalidation.** A `T&` from `write()`, or a span from `page_write()`, is invalidated
  by **any** later call on the column, including `share()` and group copies. Writing
  through a reference held across `share()` would modify the snapshot.
  - **Debug detection.** With `LITESTL_ATTR_CHECKS` (on by default without `NDEBUG`),
    `share()` records a checksum of every page that becomes shared, and the checksum is
    verified when that buffer is later copied on write or released while shared.
- **Concurrent writes** are allowed only through spans obtained serially, i.e. via
  `Column::parallel_write(first, n, f)`, which makes the pages unique and then calls
  `f(span, base)` per page, possibly on several threads. Never call `write()`, `set()`
  or structural methods concurrently.
- **Snapshots** may be read, copied and destroyed on any thread. A single column or
  group object must not be used from two threads at once, and that includes `share()`.

## Versions

`version()` returns a process-wide stamp that changes, to a never-before-seen value,
after any mutation, so stamps from diverged copies never collide. Derived caches
compare stamps. `AttrGroup::schema_version()` changes when columns are added, removed
or replaced.

## Mixing

`MixBatch` holds `targets`, `offsets` (CSR), `sources` and optional `double` `weights`.

- **Semantics are simultaneous**: every source is read from the pre-call state, then
  every target is written. Targets may alias sources (for example, swapping the two
  sides of a face).
- **An empty source list** sets the target to the default.
- **Validation.** Batches are validated once (`ValidatedMixBatch::validate`, or inside
  `AttrGroup::mix`): offsets structure, ranges, and unique targets.

| Policy | Float-like | Integral | Other |
|---|---|---|---|
| `CopyFirst`, `SetDefault`, `Skip` | ✓ | ✓ | ✓ |
| `Interpolate` (Σw·s) | double accumulation, left to right | double, round half away from zero, saturate (NaN → 0) | ✗ |
| `WeightedMean` (Σw·s / Σw; CopyFirst if Σw = 0) | as above | as above | ✗ |
| `Sum` | double accumulation | wraps in the column's own width | ✗ |
| `Custom` | column's `mix_custom` hook | | |

**Group-level mix:**
- **`Topo` columns are never mixed** at group level; topology operators write links
  themselves.
- **Policies.** `policy_of(name, slot, column)` chooses per column; without it, each
  column's default policy is used (Interpolate for float-like values, CopyFirst
  otherwise).
- **Custom handlers** receive a `MixContext` with a **pre-call snapshot** of the group
  (`pre`), the source group for `mix_from` (`src`), and operator data (`op`). A column
  can **claim** sibling slots (`claimed_slots()`); the generic loop then skips them.
- **`mix_from(src, …)`** reads from another group, matching columns by name and type
  and creating missing columns with `new_like()`.

**FP determinism.** The `attr` target sets **PUBLIC** `-ffp-contract=off -fno-fast-math
…` (or `/fp:precise`), and the kernels also open a block-scoped clang contraction
pragma. A custom `ValueOps` must follow the same rules. `test_attr_digest_fma` checks
this.

## Groups

- **Builtin slots.** An owner registers its builtin columns first, with `add_builtin`,
  and reaches them as `builtin<Column<T>>(slot)`. Nothing is cached, so copies and moves
  stay valid.
- **Dynamic columns** come from `ensure<Col>(name, default, flags)`. It returns null if
  the name exists with a different type, default or flags.
- **Copying a group** shares every column. That is a snapshot.
- **`permute(old_to_new, new_size)`** validates once (the map must be injective and in
  range, with `kDropped` for removed elements), keeps uniform pages uniform, and gives
  unmapped elements the default. To permute in parallel, call `validate_permutation`
  and then permute `column(i)` yourself.

## Canonical hash and serialisation

**Column hash:** a `Hasher128` over the type key, flags, size and default bytes,
followed by the **live element stream**. It is independent of page size,
representation and history.

**Column bytes:** a header of flags, size and default, followed by **runs** over the
element stream:
- maximal runs of at least `kRunMin` (8) bitwise-equal values become `Default` or
  `Constant` runs;
- everything else is merged into maximal `Dense` runs.

Equal contents therefore give equal bytes.

**Group hash and bytes:**
- builtins appear in slot order, then dynamic columns sorted by name;
- Temp and Derived columns are excluded from both the hash and the data;
- the stream records the full builtin layout (name, key, flags, default) and ends with
  the group content hash.

**`AttrGroup::read`:**
- checks the stream's builtin layout against the owner's;
- parses into a staging group, under `ReadLimits` checked before any allocation;
- verifies the trailing hash;
- replaces the target's contents **only on success**.

## Registry

- **What it's for.** `ColumnTypeRegistry::global()` maps type keys to column classes for
  deserialisation.
- **Registration is explicit:**
  - Builtins (all integer widths, `f32`, `f64`, `h128`, `bool`, and `{f32,f64,i32,u32}x{2,3,4}`)
    are registered on first use.
  - Modules register their own types, including `"<elem>[N]"` arrays, from their init
    routine.
  - There are no static registrar objects, because static libraries drop them.
- **Behaviour:**
  - `add<Col>()` is idempotent for the same class, and an error if a different class
    owns the key.
  - Every operation takes a mutex.
  - Storage uses std containers and `new`, so the litestl leak tracker never sees it.

## Writing a custom column class

Derive from `ColumnBase`, which must be the first base, and implement its virtuals.
Then provide:
- `using value_type`;
- `static std::string type_key()`;
- `static util::UniquePtr<ColumnBase> create(const uint8_t *default_bytes, int page_shift)`;
- `static const ColumnType &static_type()`, returning `ColumnTypeRegistry::global().add_or_die<Self>()`.

`tests/test_attr_group.cc` (`TestPairColumn`) is a complete example. Its hash and
serialisation must follow the same canonical rules.

## Relation to Sculptcore's attributes

| Sculptcore | `litestl::attr` |
|---|---|
| `AttrPage::exists` | Absent page; `litestl::attr` adds Constant pages |
| `AttrFlag::TOPO` | `ColumnFlags::Topo` |
| `AttrFlag::TEMP` | `ColumnFlags::Temp` |
| `AttrFlag::DERIVED` | `ColumnFlags::Derived` |
| `AttrMerge` DEFAULT / COPY_SRC0 / NONE / CUSTOM | `MixPolicy` Interpolate (float) or CopyFirst / CopyFirst / Skip / Custom |
| `BuiltinAttr` member pointers | `builtin<Col>(slot)` (survives copies) |
| `type_dispatch` over a closed `AttrType` set | Open set of `ColumnBase` subclasses |

`DeformPool`-style pools are planned as P1b.
