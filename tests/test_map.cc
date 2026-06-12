#include "litestl/util/map.h"
#include "litestl/util/rand.h"
#include "litestl/util/vector.h"
#include "test_util.h"
#include <cstdio>

test_init;

int test_remove()
{
  using namespace litestl::util;
  Random rand;
  Map<int, bool> set;
  Vector<int> keys;
  constexpr int size = 4096;
  int retval = 0;

  for (int i = 0; i < size; i++) {
    int key = rand.get_int();
    set.add(key, true);
    if (!keys.contains(key)) {
      keys.append(key);
    }

    for (auto &key : keys) {
      test_assert(set.contains(key));
    }
    if (rand.get_float() > 0.75) {
      int r = rand.get_int() % keys.size();
      set.remove(keys[r]);
      keys.remove_at(r, true);
    }
  }
  
  return retval;
}

/* Regression for the find_pair tombstone bug: re-inserting a key whose stale
 * value lingered in a tombstone slot could create a second live entry for the
 * same key (so remove() left a phantom behind). A small key range forces dense
 * collisions and tombstone reuse; after every op we assert the map holds each
 * present key exactly once. */
int test_no_duplicate_keys()
{
  using namespace litestl::util;
  Random rand;
  Map<int, int> map;
  Vector<int> present;     // model of live keys
  constexpr int key_range = 11;   // tiny range → dense chains + tombstone reuse
  int retval = 0;

  for (int iter = 0; iter < 20000; iter++) {
    int key = rand.get_int() % key_range;
    bool have = present.contains(key);

    if (!have && rand.get_float() > 0.4) {
      map[key] = key * 7 + 1;
      present.append(key);
    } else if (have) {
      map.remove(key);
      present.remove(key, false);
    }

    /* Live count via iteration must equal the model — a duplicate key would
     * make the map iterate the same key twice and overshoot. (map.size() is
     * not checked: remove() leaves tombstones and intentionally does not
     * decrement used_count_, so size() overcounts between rehashes.) */
    int seen = 0;
    for (auto &pair : map) {
      test_assert(present.contains(pair.key));
      test_assert(pair.value == pair.key * 7 + 1);
      seen++;
    }
    test_assert(seen == present.size());

    /* contains() must agree with the model for every key in the range. The
     * buggy find_pair matched a removed key's stale bits left in its tombstone,
     * so a just-removed key would still report present (and lookup_ptr would
     * hand back a destructed slot). */
    for (int k = 0; k < key_range; k++) {
      bool live = present.contains(k);
      test_assert(map.contains(k) == live);
      test_assert((map.lookup_ptr(k) != nullptr) == live);
      if (live) {
        test_assert(map.lookup(k) == k * 7 + 1);
      }
    }
  }

  return retval;
}

/* Regression for the copy/move special members. The move ctor used to test the
 * uninitialized destination span (always false) and never invalidate the source,
 * double-freeing a heap table and dangling on an inline one; the copy ctor
 * assigned into unconstructed cells. A non-trivial value type (Vector) forces the
 * construct-don't-memcpy paths, and n=4 vs n=400 covers inline vs heap storage. */
int test_move_copy()
{
  using namespace litestl::util;
  int retval = 0;

  for (int n : {4, 400}) {
    Map<int, Vector<int>> src;
    for (int i = 0; i < n; i++) {
      Vector<int> v;
      v.append(i);
      v.append(i * 2);
      src.add(i, v);
    }

    /* Copy ctor: src must remain valid and independent. */
    Map<int, Vector<int>> copy = src;
    for (int i = 0; i < n; i++) {
      test_assert(copy.contains(i));
      test_assert(copy.lookup(i).size() == 2);
      test_assert(copy.lookup(i)[0] == i && copy.lookup(i)[1] == i * 2);
      test_assert(src.contains(i));
    }

    /* Move ctor. */
    Map<int, Vector<int>> moved = std::move(src);
    int seen = 0;
    for (auto &pair : moved) {
      seen++;
    }
    test_assert(seen == n);
    for (int i = 0; i < n; i++) {
      test_assert(moved.contains(i));
      test_assert(moved.lookup(i)[1] == i * 2);
    }

    /* Move assignment over a populated target (must free the target's table). */
    Map<int, Vector<int>> target;
    target.add(-1, Vector<int>{});
    target = std::move(moved);
    for (int i = 0; i < n; i++) {
      test_assert(target.contains(i));
      test_assert(target.lookup(i)[0] == i);
    }
    test_assert(!target.contains(-1));
  }

  return retval;
}

/* Regression for the Vector move-ctor capacity lie (see test_vector.cc)
 * triggered through the rehash: buckets holding exactly static_size elements
 * (heap-backed) were moved into the new table pointing at inline storage but
 * keeping the heap capacity_, so later appends overflowed into the neighboring
 * Pair — and a subsequent rehash stole the inline pointer into the freed old
 * table (use-after-free). Mimics the triage weld-grid usage pattern. */
int test_rehash_bucket_overflow()
{
  using namespace litestl::util;
  int retval = 0;
  Map<int, Vector<int>> grid;
  constexpr int keys = 257;

  /* Bring every bucket to exactly 4 elements (Vector<int>'s static_size). */
  for (int j = 0; j < 4; j++) {
    for (int k = 0; k < keys; k++) {
      grid[k].append(k * 16 + j);
    }
  }
  /* Force at least one rehash while the buckets sit in that state. */
  for (int k = keys; k < keys * 4; k++) {
    grid[k].append(k * 16);
  }
  /* Append past the inline capacity of every original bucket. */
  for (int j = 4; j < 8; j++) {
    for (int k = 0; k < keys; k++) {
      grid[k].append(k * 16 + j);
    }
  }

  for (int k = 0; k < keys; k++) {
    Vector<int> *bucket = grid.lookup_ptr(k);
    test_assert(bucket != nullptr);
    test_assert(bucket->size() == 8);
    for (int j = 0; j < 8; j++) {
      test_assert((*bucket)[j] == k * 16 + j);
    }
  }
  for (int k = keys; k < keys * 4; k++) {
    test_assert(grid.lookup_ptr(k) != nullptr);
    test_assert(grid.lookup(k)[0] == k * 16);
  }

  return retval;
}

/* Regression: operator[] left simple (int/pointer/float) values uninitialized
 * on first insert, so counting patterns like map[k]++ started from garbage. */
int test_operator_brackets_init()
{
  using namespace litestl::util;
  int retval = 0;
  Map<int, int> count;

  for (int pass = 0; pass < 3; pass++) {
    for (int k = 0; k < 1000; k++) {
      count[k * 7919]++;
    }
  }
  for (int k = 0; k < 1000; k++) {
    test_assert(count.lookup(k * 7919) == 3);
  }

  return retval;
}

int main()
{
  using namespace litestl::util;

  test_remove();
  test_no_duplicate_keys();
  test_move_copy();
  test_rehash_bucket_overflow();
  test_operator_brackets_init();

  {
    Map<int, int> imap;
    Random rand;

    const int size = 512;
    Vector<int> keys, values;

    for (int i = 0; i < size; i++) {
      int key = rand.get_int();
      int value = rand.get_int();

      imap.add(key, value);
      keys.append(key);
      values.append(value);

      test_assert(imap.contains(key));
      test_assert(imap.lookup(key) == value);
    }

    for (auto &pair : imap) {
      test_assert(keys.contains(pair.key));
      test_assert(values.contains(pair.value));

      keys.remove(pair.key);

      for (auto &key : keys) {
        test_assert(imap.contains(key));
      }
    }

    test_assert(keys.size() == 0);
  }

  return test_end();
}
