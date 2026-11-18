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
| `attr/schema.h` | `AttrDef`, `Schema`, `TypedGroup`: compile-time named attributes |

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

- **Columns.** `version()` returns a process-wide stamp that changes, to a
  never-before-seen value, after any mutation, so stamps from diverged copies never
  collide. Derived caches compare stamps.
- **Column set.** `AttrGroup::schema_version()` changes when columns are added, removed
  or replaced.
- **Whole group.** `AttrGroup::version()` is the maximum of `schema_version()` and every
  column stamp. Every mutation produces a stamp newer than all stamps issued before it,
  so this also changes after any mutation. A snapshot has its source's stamp until one
  of them is mutated.

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

**NaN results.** Arithmetic policies return the canonical quiet NaN for NaN results.
That's because IEEE leaves arithmetic NaN payloads unspecified and compilers may
swap commutative operands. `CopyFirst` preserves payloads.

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

## Compile-time schemas (`attr/schema.h`)

Client code should not index builtin slots by hand. Instead, declare each attribute
**once**, as a type that binds a name (a string literal template parameter, using the
same `util::StrLiteral` as Sculptcore's `BuiltinAttr`), a column type, flags and a
default:

```cpp
using Pos = attr::AttrDef<"pos", attr::Column<math::double3>>;
struct VertEdge : attr::AttrDef<".v.e", attr::Column<uint32_t>, attr::ColumnFlags::Topo> {
  static uint32_t default_value() { return 0xffffffffu; }   // shadow for a custom default
};
using VertexSchema = attr::Schema<Pos, VertEdge>;
using MyVertexSchema = VertexSchema::extend<attr::AttrDef<"weight", attr::Column<float>>>;

attr::TypedGroup<VertexSchema> verts;
verts.get<"pos">().set(i, p);          // Column<double3>&, resolved at compile time
verts.get<VertEdge>()[i];              // or by definition type
constexpr uint32_t s = attr::TypedGroup<VertexSchema>::slot<".v.e">();
```

**How it works:**
- `TypedGroup<S>` registers `S`'s attributes as the group's builtins, in order.
  `get<"name">()` is a compile-time lookup of the slot plus a typed `builtin<>()` access.
- `group()` exposes the underlying `AttrGroup` for mixing, permuting and I/O.
- `read()` verifies that a stream's builtin layout matches the schema.

**Errors caught at compile time** (`tests/compile_fail/`):
- an unknown name (`get<"position">()`);
- duplicate names in a schema;
- a definition that isn't in the schema;
- a non-column type in `AttrDef`;
- binding a column to the wrong type.

**Optional (non-builtin) attributes** reuse definitions through
`attr::ensure<Def>(group)` and `attr::find<Def>(group)`, so a name is never spelled apart
from its type, default and flags.

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

**Partial hashes.** `AttrGroup::hash_into(h, keep)` hashes only the columns that the
filter `keep(name, column)` accepts. It uses the same encoding, so a keep-all filter
gives the plain hash. Temp and Derived columns are excluded before the filter runs. A
typical use is a topology hash that leaves out geometry columns.

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

**A custom column whose values hold `PoolRef`s must override the ref hooks:**
- `holds_refs`;
- `visit_refs`, which also visits the default with `count = 0`;
- `remap_refs`;
- `remap_refs_rows`;
- `visit_elem_refs`;
- `remap_elem_refs`.

Without them its refs are invisible to `PoolStore::collect`, and their entries get
freed. `RefPairColumn` in `tests/pool_test_types.h` is an example.

## Value pools (`attr/pool.h`, `attr/pool_store.h`)

A `PoolStore` owns immutable, deduplicated entries of any size, one
`InternPool<T>` per kind. Columns refer to entries with `PoolRef { pool, slot }`
values (`Column<PoolRef>`, type key `"ref"`). `pool == 0` means none.

**Interning.**
- `store.intern<P>(pool_id, value)` returns the existing slot when an equal entry is
  live, and otherwise takes a free slot (LIFO, lowest index first after a collect).
  So within a pool, ref equality is value equality.
- **Entries are immutable.** To edit, intern the new value and reassign the column.

**Value types.** `PoolTraits<T>` covers:
- arithmetic types, `math::Vec` and `std::array` of them (raw bytes);
- `util::string`;
- any type with the member protocol: `hash_into`, `equal` (bitwise for floats; not
  `operator==`), `write`, `static read`, and `static constexpr bool has_refs`. When
  `has_refs` is true, `for_each_ref` and `remap_refs` are also required.

A struct holding a `PoolRef` must declare `has_refs = true`. Refs inside entries keep
their targets alive, and they must point into the same store.

**Liveness is mark-and-sweep.** A `PoolRef` value does not keep its entry alive.
`store.collect(roots)` frees every entry not reachable from:
- the ref columns of the root groups (Temp and Derived included);
- the extra roots (`RefRoot`: a journal, a weak cache map via `prune`);
- refs inside reachable entries.

Rules for owners:
- **Collect at deterministic points:** operation boundaries, a threshold on
  `taken_since_collect()`, before `canonicalize` or `write`. Slot reuse depends on
  where collects happen.
- **Rooting rule.** Never hold a `PoolRef` anywhere but a root across `collect` or
  `canonicalize`. A forgotten root has its entry freed, and the slot can then be
  reused for a different value. `set_reuse_freed(false)` (tests) quarantines freed
  slots, so `audit` reports such dangling refs.
- **`collect` validates while marking.** It fails without changes on a dangling,
  malformed (`{0, s ≠ 0}`) or refused (`Roots::accept`) ref, or on a ref column whose
  default isn't `kNoRef`.

**Snapshots.**
- `PoolStore` is a value type, and copying it is O(slot pages):
  - slot pages and the dedup index are shared copy-on-write;
  - entry nodes are shared between snapshots through an atomic count.
- Nothing points into a store, so an owner (store plus groups) keeps defaulted copy
  and move.
- Snapshots diverge independently and may be used on different threads.
- Each store has one writer at a time.

**Canonical form.**
- `canonicalize(roots)` renumbers slots by first reference, in three phases, each
  closed over inner refs:
  1. hashed columns, in group order and `canonical_order()`, elements ascending;
  2. Temp and Derived columns;
  3. extra roots.

  It drops unreachable entries and compacts. Only the phase-1 closure is hashed and
  written, so caches, undo history and sibling snapshots never change the bytes.
- `hash_into`, `content_hash` and `write` take the roots, and require
  `canonical(roots)`: neither the store nor any root group may have changed since.

**Owner write:**
1. copy the owner;
2. `canonicalize` the copy;
3. write the store, then the groups.

**Owner read:**
1. build a fresh owner;
2. `store.read`, then each group's `read`;
3. `audit(roots, require_reachable = true)`;
4. `canonicalize`, which must report `changed == false`;
5. move-assign.

**Between stores.**
- **A raw `PoolRef` copied between stores is meaningless.** Use
  `import(src, ref, memo)` (a deep re-intern with pools matched by key), or
  `import_rows(column, rows, src, memo)` after a cross-owner `mix_from`.
- `PoolStore::version()` changes whenever slots may have been reused, so memos keyed
  by `PoolRef` can detect staleness.

`tests/test_attr_pool*.cc` and `tests/pool_test_types.h` show the whole owner pattern.
`bench_attr_pool` benchmarks the index and copy costs.

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

`DeformPool`-style pools are `InternPool` / `PoolStore` (above), with mark-and-sweep
instead of refcounts.
