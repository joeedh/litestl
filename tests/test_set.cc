#include "litestl/util/rand.h"
#include "litestl/util/set.h"
#include "litestl/util/string.h"
#include "litestl/util/vector.h"
#include "test_util.h"
#include <cstdio>

test_init;

int test_remove()
{
  using namespace litestl::util;
  Random rand;
  Set<int> set;
  Vector<int> keys;
  constexpr int size = 4096;
  int retval = 0;

  for (int i = 0; i < size; i++) {
    int key = rand.get_int();
    set.add(key);
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

/* Stress the control-byte plane at the 7/8 load factor: a wide key range with
 * heavy interleaved add/remove drives tombstone creation, reclamation, and
 * rehashes. The live set (via iteration) must match the model and the internal
 * invariants (ctrl == h2, live count, cloned tail) must hold. */
int test_high_load_churn()
{
  using namespace litestl::util;
  Random rand;
  Set<int> set;
  Vector<int> present;
  constexpr int key_range = 2003;
  int retval = 0;

  for (int iter = 0; iter < 60000; iter++) {
    int key = rand.get_int() % key_range;
    bool have = present.contains(key);

    if (!have && rand.get_float() > 0.35) {
      set.add(key);
      present.append(key);
    } else if (have) {
      set.remove(key);
      present.remove(key, false);
    }

    test_assert(set.size() == size_t(present.size()));

    if ((iter & 1023) == 0) {
      test_assert(set.debugCheckInvariants());
      int seen = 0;
      for (int k : set) {
        test_assert(present.contains(k));
        seen++;
      }
      test_assert(seen == present.size());
    }
  }

  for (int k = 0; k < key_range; k++) {
    test_assert(set.contains(k) == present.contains(k));
  }
  test_assert(set.debugCheckInvariants());

  return retval;
}

/* A near-group-width inline table (static_size_logical=1) keeps probing wrapping
 * through the cloned tail. Fill past the inline group, remove and re-add, and
 * verify lookups + invariants survive the wrap and inline→heap transition. */
int test_tiny_table_wrap()
{
  using namespace litestl::util;
  int retval = 0;
  Set<int, 1> set;

  for (int k = 0; k < 200; k++) {
    set.add(k);
    test_assert(set.debugCheckInvariants());
  }
  for (int k = 0; k < 200; k++) {
    test_assert(set.contains(k));
  }
  for (int k = 0; k < 200; k += 2) {
    test_assert(set.remove(k));
  }
  test_assert(set.debugCheckInvariants());
  for (int k = 0; k < 200; k++) {
    test_assert(set.contains(k) == (k % 2 != 0));
  }
  for (int k = 0; k < 200; k += 2) {
    set.add(k);
  }
  test_assert(set.debugCheckInvariants());
  for (int k = 0; k < 200; k++) {
    test_assert(set.contains(k));
  }

  return retval;
}

/* Pointer keys exercise hash(T*) (shifted pointer) run through mixHash. */
int test_pointer_keys()
{
  using namespace litestl::util;
  int retval = 0;
  constexpr int n = 1500;
  Vector<int> storage;
  storage.resize(n);

  Set<int *> set;
  for (int i = 0; i < n; i++) {
    set.add(&storage[i]);
  }
  test_assert(set.size() == size_t(n));
  test_assert(set.debugCheckInvariants());
  for (int i = 0; i < n; i++) {
    test_assert(set.contains(&storage[i]));
  }
  int dummy = 0;
  test_assert(!set.contains(&dummy));

  for (int i = 0; i < n; i += 3) {
    test_assert(set.remove(&storage[i]));
  }
  test_assert(set.debugCheckInvariants());
  for (int i = 0; i < n; i++) {
    test_assert(set.contains(&storage[i]) == (i % 3 != 0));
  }

  return retval;
}

int main()
{
  using namespace litestl::util;

  if (int ret = test_high_load_churn()) {
    return ret;
  }
  if (int ret = test_tiny_table_wrap()) {
    return ret;
  }
  if (int ret = test_pointer_keys()) {
    return ret;
  }

  {
    static constexpr int size = 512;

    Set<int, size + 10> set;
    Random rand;

    Vector<int> keys;

    for (int i = 0; i < size; i++) {
      int key = rand.get_int();

      set.add(key);
      keys.append(key);

      test_assert(set.contains(key));
    }

    for (int key : keys) {
      test_assert(set.contains(key));
    }

    for (int key : set) {
      test_assert(keys.contains(key));
    }

    const char *strings[] = {"sdfdsf", "bleh", "yay", "space123324", "hahah", "gagaga"};

    Set<string> strset;

    for (int i = 0; i < array_size(strings); i++) {
      strset.add(strings[i]);
    }

    test_assert(!strset["dsfdsf"]);

    for (const string &str : strset) {
      test_assert(strset[str]);
    }

    if (int ret = test_remove()) {
      return ret;
    }
  }

  return test_end();
}
