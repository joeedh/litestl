#pragma once

/* PoolRef: a reference to an entry of a pool in a PoolStore (attr/pool_store.h),
 * plus the visitor interfaces columns and other ref holders implement so the
 * store can mark, validate and renumber the refs they hold.
 *
 * PoolRef is a plain 8-byte value: copying it does not keep its entry alive.
 * Liveness comes from PoolStore::collect(roots), which frees every entry that
 * isn't reachable from the roots the owner passes in. */

#include <cstdint>
#include <array>
#include <string>
#include <type_traits>

#include "attr/type_key.h"

namespace litestl::attr {

struct PoolRef {
  uint32_t pool = 0; /* 1-based pool id in the store; 0 = none */
  uint32_t slot = 0;

  constexpr bool none() const
  {
    return pool == 0;
  }
  constexpr bool operator==(const PoolRef &b) const = default;
};
static_assert(sizeof(PoolRef) == 8);

inline constexpr PoolRef kNoRef{};

/** {0, s != 0} is malformed: kNoRef has exactly one representation. */
constexpr bool well_formed(PoolRef r)
{
  return r.pool != 0 || r.slot == 0;
}

/** Value types that hold PoolRefs which Column<T> would not report (B8). */
template <typename T> struct is_pool_ref_aggregate : std::false_type {};
template <size_t N> struct is_pool_ref_aggregate<std::array<PoolRef, N>> : std::true_type {};

template <> struct ValueKey<PoolRef> {
  static std::string key()
  {
    return "ref";
  }
};

/** Receives refs. count is the number of elements holding r (Constant pages
 * report once); a column reports its default value with count 0. */
class RefVisitor {
public:
  virtual void visit(PoolRef r, uint64_t count) = 0;

protected:
  ~RefVisitor() = default;
};

/** Maps refs during renumbering or import. map(kNoRef) must be kNoRef. */
class RefRemap {
public:
  virtual PoolRef map(PoolRef r) const = 0;

protected:
  ~RefRemap() = default;
};

/** Mark state, handed to RefRoot::prune between mark and sweep. */
class MarkQuery {
public:
  virtual bool marked(PoolRef r) const = 0;

protected:
  ~MarkQuery() = default;
};

/** Where a ref was found, for the owner's accept() validator. */
struct RefSite {
  enum Kind : uint8_t { Column = 0, Inner = 1, Extra = 2 };
  Kind kind = Column;
  uint32_t a = 0; /* Column: group index; Inner: pool id; Extra: root index */
  uint32_t b = 0; /* Column: column index; Inner: slot */
};

} // namespace litestl::attr
