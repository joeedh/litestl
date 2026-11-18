# Numerics and determinism

This page covers exact arithmetic, robust predicates, stable hashing, counter-based
randomness and deterministic parallel reduction. Together they are the foundation for
reproducible geometry: the same inputs give bit-identical results on any machine and
any thread count.

| Header | Namespace | What |
|---|---|---|
| `math/wideint.h` | `litestl::math` | `Int128`, `Int256` (`WideInt<N>`) |
| `math/predicates.h` | `litestl::math::pred` | `orient2d`, `orient3d`, `incircle`, `insphere` |
| `util/hash128.h` | `litestl::hash` | `Hash128`, `SipHash128`, `Hasher128`, canonical encoders |
| `util/rng.h` | `litestl::rng` | `philox4x32`, `Stream` |
| `util/task.h` | `litestl::task` | `parallel_for_chunks`, `parallel_reduce`, `parallel_exclusive_scan`, `ScopedBandCount` |
| `util/sort.h` | `litestl::util` | `sort_unique_keys`, `stable_sort` |

## Wide integers (`math/wideint.h`)

- **Representation.** `WideInt<N>` is N little-endian 64-bit words in two's complement.
  `Int128 = WideInt<2>` and `Int256 = WideInt<4>`.
- **Wrapping arithmetic.** Arithmetic wraps, like unsigned built-ins. Size the type so
  that the **final** result fits; intermediate overflow is harmless.
- **Operations:** `+ - * ~ << >>` (shift counts below the bit width), `<=>`, `sign()`,
  `from()` (sign-extend) and an approximate `to_double()`. There is **no division**.
- **Exact products:** `mul_wide(int64, int64) -> Int128` and
  `mul_wide(WideInt<N>, WideInt<N>) -> WideInt<2N>`.
- **64×64→128 product implementations:**
  - `detail::portable` (32-bit halves) is always compiled.
  - `unsigned __int128` is used where available.
  - `-DLITESTL_NO_INT128=ON` (CMake) forces the portable path everywhere. It is a
    PUBLIC define on `math`, so every consumer sees the same choice.
  - The struct layout never depends on which path is used.

## Predicates (`math/predicates.h`)

Every predicate returns the **exact sign** (−1, 0, +1) of its determinant. The sign
conventions are Shewchuk's:

| Call | Positive when |
|---|---|
| `orient2d(a, b, c)` | a, b, c are counter-clockwise |
| `orient3d(a, b, c, d)` | d is below the plane through a, b, c (a, b, c counter-clockwise seen from above) |
| `incircle(a, b, c, d)` | d is inside the circle through counter-clockwise a, b, c |
| `insphere(a, b, c, d, e)` | e is inside the sphere through a, b, c, d, where `orient3d(a, b, c, d) > 0` |

### `double`

- **Algorithm.** Adaptive-precision exact arithmetic, a mechanical port of
  Shewchuk's public-domain `predicates.c`. See `math/predicates/generator/` for how
  the port was made.
- **Exact domain.** Inputs that are 0, or whose magnitude is in [2⁻¹⁵⁰, 2²⁰⁰].
  - Every exact product component then stays at or above 2⁻¹⁰²², so results stay
    exact even when FTZ/DAZ is enabled process-wide (e.g. by another TU linked with
    `-ffast-math`).
  - The binding bound is insphere: degree 5 with 52-bit mantissas,
    5(m + 52) ≤ 1022.
  - Debug builds abort on inputs outside the domain.
- **Stack and heap.** Stack use is ≤ ~33 KB (insphere). The exact insphere stage uses
  ~180 KB of **heap** scratch (`litestl::alloc`), so small thread stacks are fine;
  tests run on a 64 KB stack.
- **Compiler flags.** The implementation is the `predicates` library, compiled with
  `-ffp-contract=off -fno-fast-math -fno-associative-math` (or `/fp:precise` under
  an MSVC-style driver), with `#pragma` backups.
  - `#error` if `__FAST_MATH__` is defined.
  - `static_assert(FLT_EVAL_METHOD == 0)`.
- **`fp_selftest()`** runs inside those translation units and detects FMA contraction
  and reassociation.

### `float`

`float` inputs are converted to `double` (exactly), with no domain restriction.

### `int64` lattice

`template <int Bits = 26>` overloads take `int64_2` / `int64_3`, with |x| ≤ 2^Bits.
Accumulator widths follow Hadamard's bound on (Bits+1)-bit differences:

| Predicate | Bound | Accumulator at Bits = 26 |
|---|---|---|
| orient2d | 2^(2B+3) | int64 (55 bits) |
| orient3d | 2^(3B+6.4) | Int128 (85 bits) |
| incircle | 2^(4B+7.4) | Int128 (113 bits) |
| insphere | 2^(5B+10.6) | Int256 (142 bits) |

`static_assert`s tie each predicate's `Bits` to its accumulator. Inputs are
range-checked, aborting on violation, unless `LITESTL_PRED_CHECK_RANGE=0`.

### Diagnostics

- **Stage counters.** `stage_hits(Predicate, Stage)` counts per stage:
  - A: static filter;
  - B, C: adaptive stages;
  - T: early exit because the coordinate differences were exact;
  - D: fully exact stage.
- **Counters are opt-in.** They are only non-zero when linking `predicates_stats`
  instead of `predicates`. Link one or the other.

## Hashing (`util/hash128.h`)

- **`SipHash128` / `hash128()`** implement SipHash-2-4 with 128-bit output, as defined
  by the [reference implementation](https://github.com/veorq/SipHash). Tests check all
  64 official vectors.
- **No adversarial resistance.** The default key is public, so no adversarial
  collision resistance is claimed.
- **`Hasher128(DomainTag)`** is a tuple hasher.
  - It always writes the **domain tag first**, so different kinds of hash never share
    a space. litestl reserves domain values below 256.
  - Each typed encoder (`u8 u32 i32 u64 i64 boolean f32_bits f64_bits f64_canonical
    h128 str`) writes a **1-byte type tag** and then a little-endian payload, so
    tuples can't collide structurally.
  - `str` is u64-length-prefixed.
  - `raw_bytes` is untagged, for content hashing of already-canonical buffers.
- **Choosing the f64 encoder:**
  - `f64_canonical` maps −0 to +0 and every NaN to `0x7ff8000000000000`. Use it for
    **identity** derived from geometry.
  - `f64_bits` keeps raw bits. Use it for **content** hashes, where −0 and +0 must
    differ.
- **`Hash128`** has `<=>` (hi word, then lo), `computeHash()` (so it can be used as a
  `Map`/`Set` key), LE bytes and `hex()`.
- **Golden values.** Outputs are part of the determinism contract. Golden values are
  pinned in `tests/test_hash128.cc`.

## Random numbers (`util/rng.h`)

- **Generator.** `philox4x32<Rounds>(Counter, Key)` is the Philox bijection. Tests
  check the Random123 known-answer vectors.
- **`Stream(Hash128 key)`** gives keyed, **stateless** draws. Results depend only on
  (key, element, draw), never on call order or thread count.
  - The Philox key is `key.lo ^ mixHash(key.hi)`.
  - The counter is `{element lo32, element hi32, draw, block}`.
  - `u32`, `u64`, `uniform` (53 bits) and `uniformf` (24 bits) use block 0.
  - `range(element, lo, hi_excl, draw)` is unbiased (Lemire multiply-shift with
    rejection). Its rejection draws consume blocks 1, 2, … of the same
    (element, draw), never `draw + 1`.
- **`element_of(Hash128 id)`** folds a 128-bit persistent ID to a 64-bit element.
- **Golden values.** The layout is pinned by golden values in `tests/test_rng.cc`.

## Deterministic parallel reduction (`util/task.h`)

`parallel_for` splits work into `min(worker_count, tasks)` bands, so band boundaries
depend on the machine. A floating-point reduction done per band is therefore not
reproducible. Instead:

- **`parallel_for_chunks(range, chunk, cb(chunk_index, range))`** fixes chunk
  boundaries by `chunk` alone. `chunk` must be a caller constant, never derived from
  hardware.
- **`parallel_reduce(range, chunk, identity, map, combine)`** stores per-chunk results
  by index and left-folds them on the calling thread. The result is bit-identical on
  any machine, even for non-associative (floating-point) `combine`.
- **Nesting.** Both run serially when called from a pool worker, which avoids
  nested-wait starvation. The result is unchanged.
- **`ScopedBandCount(n)`** is a thread-local override of `parallel_for`'s band count,
  clamped to `[1, LITESTL_WORKERS_COUNT]`. Tests use it to reproduce, on a small CI
  machine, the band layouts of a large one.
- **`parallel_exclusive_scan(in, out, chunk)`** computes an exclusive prefix sum over
  unsigned integers, with wrapping arithmetic, and returns the total. `out` may alias
  `in`. Integer addition is associative, so the result never depends on the chunking.

## Deterministic sorting (`util/sort.h`)

The standard library doesn't fix the output order of tied keys: `std::sort` can differ
between implementations, and so can `std::stable_sort`'s internal strategy, although
its result can't. litestl offers two reproducible options:

- **`sort_unique_keys(span, less, &tie_index)`** is for comparators that are a strict
  total order on the input, such as keys ending in a persistent ID.
  - Any correct sort then gives the same permutation, so it uses `std::sort`.
  - Ties are checked in **every** build type. A tie returns `false` with its sorted
    position, because a tie is a caller bug (a duplicate ID), never something to
    resolve silently.
- **`stable_sort(span, less)`** is a self-contained bottom-up merge sort, for the rare
  inputs with genuine ties. Tied elements keep their input order.

## Tests and determinism digest

- **Test files:**
  - `test_wideint`, `test_predicates`: randomised suites against the independent
    `tests/bigint_oracle.h`;
  - `test_hash128`, `test_rng`: official vectors and golden values;
  - `test_task`: reduction determinism with a negative control;
  - `test_determinism_digest`: one pinned hash over all of the above.
- **Case counts** scale with the `LITESTL_TEST_SCALE` environment variable, or the
  CMake cache value of the same name. Heavy suites carry the ctest label `long`
  (`ctest -LE long` skips them).
- **Reporting.** `LITESTL_TEST_NO_BREAK=1` reports every failure instead of trapping on
  the first.
- **Configurations** that should all pass, with the same digest:
  - clang Release;
  - gcc Release;
  - clang `-march=x86-64-v3` (FMA available);
  - clang `-DLITESTL_NO_INT128=ON`;
  - Debug with UBSan (gcc here; clang works too where its UBSan runtime is installed).
