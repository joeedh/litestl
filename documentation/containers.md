# `litestl::util` Containers

This document introduces the core container types in `litestl::util` and the
hashing API in `util/hash.h` that the hashed containers are built on. These
containers are the preferred replacements for `std::vector`, `std::unordered_set`,
`std::unordered_map`, and `std::vector<bool>` in engine code — in particular in
hot paths, where STL allocation behavior and codegen are undesirable.

All of these types share a few design traits:

- **Small-buffer optimized.** Each container carries a templated inline
  capacity; allocations to `alloc::alloc` only happen when that capacity is
  exceeded. This keeps short-lived containers allocation-free.
- **Non-throwing.** The containers do not use exceptions. Out-of-range access
  is undefined behavior; check `size()` / `contains()` first where needed.
- **WASM-aware alignment.** Classes are marked
  `alignas(ContainerAlign<T>())` so that inline storage alignment is correct
  under Emscripten, where `void*` is 4 bytes but `double`/64-bit scalars still
  need 8-byte alignment.
- **Trivial-type fast path.** Copy/move/destroy loops check
  `is_simple<T>()` and drop to `memcpy` / skip destructors when possible.

All types live in `namespace litestl::util`.

---

## `Vector<T, static_size = 4>`

`util/vector.h` — small-buffer-optimized dynamic array.

Stores up to `static_size` elements inline; grows on the heap past that.
Supports range-for, `std::ranges` algorithms, and random-access iterators.

### Construction

```cpp
Vector<int> a;                         // empty
Vector<int> b{1, 2, 3, 4};             // initializer list
Vector<int> c(ptr, count);             // move-from raw array
```

### Core API

| Method | Notes |
| --- | --- |
| `append(const T&)` / `append(T&&)` | Push to end. |
| `append_once(v)` | Push only if not already present (O(n) linear search). |
| `prepend(v)` | Insert at front; O(n). |
| `pop_back()` | Remove and return last. |
| `pop_front()` | Remove and return first; O(n). |
| `grow_one(args...)` | Placement-construct a new element in-place. |
| `remove(value, swap_end_only=false)` | Remove first occurrence. With `swap_end_only`, fills the gap from the end (O(1), unordered). |
| `remove_at(i, swap_end_only=false)` | Same, but by index. |
| `index_of(v)` | Linear search; returns `-1` if absent. |
| `contains(v)` | Convenience wrapper around `index_of`. |
| `resize<construct_destruct=true, shrink_only=false>(n)` | Resize; template flags let you skip ctor/dtor work on resize. |
| `ensure_capacity(n)` | Reserve without changing size. |
| `clear()` | Destruct elements, keep capacity. |
| `clear_and_contract()` | Destruct elements and release heap memory, returning to static storage. |
| `contract()` | Shrink capacity down to size (or back to static storage). |
| `sort()` / `sort(cb)` | In-place sort. Comparator returns negative/zero/positive (`a - b` style), not a bool. See `VectorSortComparator`. |
| `reverse()` | In-place reverse; returns `*this`. |
| `slice(start)` / `slice(start, end)` | Copy a sub-range. Negative `end` is relative to size. |
| `join(sep)` | Fold with separator (useful for string-like `T`). |
| `size()`, `data()`, `last()`, `operator[]` | The obvious accessors. |
| Implicit conversion to `std::span<T>` / `std::span<const T>`. |

### Comparator convention

`sort(cb)` and anything taking `VectorSortComparator<T>` uses the
subtraction-style convention:

```cpp
vec.sort([](const T &a, const T &b) -> int32_t {
  return a.priority - b.priority;   // <0, 0, >0
});
```

### Gotchas

- `Vector(T*, count)` *moves* from the source; the input is left in a
  moved-from state.
- Iterators hold a pointer back to the vector — don't persist them across
  reallocations (append/resize that grows capacity).

---

## `Set<Key, static_size_logical = 4>`

`util/set.h` — open-addressing hash set with quadratic probing.

Inline capacity is `static_size_logical` *logical* entries; the actual table
is ~4x that to maintain load factor. Rehashes when occupancy exceeds ~1/3 of
the table. Uses tombstones (`clearmap_`) so deletes don't break probe chains.

### API

| Method | Notes |
| --- | --- |
| `add(key)` | Insert if absent. Returns `true` if inserted. |
| `remove(key)` | Returns `true` if removed. |
| `contains(key)` / `operator[](key)` | Membership test. |
| `size()` | Current entry count. |
| `clear()` | Destruct keys, reset. Returns `*this`. |
| Range-for | Iterates keys in table order (not insertion order). |

`Key` must be hashable via `litestl::hash::hash` (see below) and support
`operator==`.

---

## `Map<Key, Value, static_size = 16>`

`util/map.h` — open-addressing hash map with quadratic probing. Same probing
and tombstone scheme as `Set`; inline table is sized `static_size * 3 + 1`.

### Construction

```cpp
Map<int, string> m;
Map<int, string> n{{1, "a"}, {2, "b"}};
```

### API

| Method | Notes |
| --- | --- |
| `add(k, v)` | Insert if absent; returns `true` if new. Does not overwrite. |
| `add_overwrite(k, v)` | Insert or overwrite; returns `true` if the key was new. |
| `insert(k, v)` | **Unchecked** insert — caller must guarantee `k` is absent, else duplicates result. |
| `operator[](k)` | Default-constructs a value if missing; returns reference. |
| `contains(k)` | Membership test. |
| `lookup(k)` | Reference to value. UB if missing — use `contains()` or `lookup_ptr()` first. |
| `lookup_ptr(k)` | Pointer to value, or `nullptr`. |
| `remove(k, out=nullptr)` | Remove; if `out` is non-null, moves the value into it. |
| `add_uninitialized(k, &vptr)` | Insert if absent, hand back a pointer to the value slot (default-initialized for non-trivial types) so the caller can assign. |
| `add_callback(k, copy_key, make_value)` | Insert-if-absent with caller-controlled key copy and value construction; returns a reference to the value (existing or new). |
| `keys()` / `values()` | Range-for iterables over keys / values. |
| `begin()` / `end()` | Iterator yielding `Pair<Key, Value>&`. |
| `reserve(n)` | Ensure capacity for `n` entries. |
| `size()` | Current entry count. |

Iteration order is table order — don't rely on insertion order.

---

## `BoolVector<static_size = 32>`

`util/boolvector.h` — packed bit vector, one bit per element, 32-bit blocks.

Use this instead of `std::vector<bool>` or `Vector<bool>` when you need
hundreds-to-thousands of flags. Inline capacity is `static_size` bits
(rounded up to a 32-bit block).

### API

| Method | Notes |
| --- | --- |
| `operator[](i) -> bool` | Read a bit. |
| `set(i, val) -> bool` | Write a bit; returns the old value. |
| `append(val)` | Grow by one bit. |
| `resize(n)` | Grow logical size to `n` bits (reallocates blocks if needed). |
| `clear()` | Zero all bits, keep capacity. |
| `reset()` | Set logical `used_` count to 0 without zeroing memory. |
| `size()` | Number of bits in use (`used_`). |

`BoolVector` is used internally by `Set` and `Map` for their occupancy and
tombstone masks.

---

## `litestl::hash` — `util/hash.h`

A tiny hashing front-end used by `Set` and `Map`. Everything returns a
`HashInt` (alias for `uint64_t`).

```cpp
namespace litestl::hash {
using HashInt = uint64_t;

template <typename T> HashInt hash(T *ptr);   // pointer hash (shifts out low bits)
HashInt hash(int i);                           // identity
HashInt hash(const char *str);                 // C-string — NOT cryptographic
HashInt hash(const util::string &s);           // forwards to c_str overload
HashInt hash(const util::stringref &s);
}
```

Notes:

- The pointer overload shifts pointers left by 16 on 64-bit platforms, since
  only the low 48 bits of a pointer carry entropy on current architectures.
- The string hash is a small mixing loop and is **not** cryptographically
  secure — do not use it for anything security-sensitive.
- To make your own type hashable by `Set` / `Map`, add an overload in the
  `litestl::hash` namespace:

  ```cpp
  namespace litestl::hash {
  inline HashInt hash(const MyKey &k) { return hash(k.id) ^ hash(k.name); }
  }
  ```

  Your type must also provide `operator==`.

---

## Choosing a container

| Need | Use |
| --- | --- |
| Ordered sequence, mostly appends | `Vector<T>` |
| Tight set of flags | `BoolVector<>` |
| Membership test by key | `Set<Key>` |
| Key → value lookup | `Map<Key, Value>` |
| Fixed-size compile-time array | `util::Array<T, N>` (see `util/array.h`) |
| Non-owning view | `std::span<T>` / `util::Span<T>` |

When in doubt, prefer these over STL equivalents in `source/` — they
integrate with `alloc::alloc` accounting, avoid exception machinery, and
keep small containers heap-free.
