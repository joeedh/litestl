# Predicate port generator

`orient.cc`, `incircle.cc`, `insphere.cc`, `expansion.cc` and `expansion.h`
(except where noted) are generated from Jonathan Richard Shewchuk's
public-domain `predicates.c` by `port_predicates.py`:

    python3 port_predicates.py path/to/predicates.c path/to/litestl/math

The script turns the expansion-arithmetic macros into inline functions with
the same call syntax (inputs by value, outputs by reference), copies the
adaptive predicates verbatim (REAL -> double), inserts stage counters at each
return, and moves `insphereexact`'s ~180 KB of scratch arrays to the heap.
`api.cc` (public wrappers, diagnostics, `fp_selftest`) is hand-written.

Prefer regenerating over hand-editing the generated files. The reference
source used was the mirror at
https://github.com/danshapero/predicates/blob/master/src/predicates.c
(Shewchuk's own host, cs.cmu.edu, was not reachable when this was generated;
the mirror carries Shewchuk's public-domain header and was validated by the
oracle tests in `tests/test_predicates.cc`).
