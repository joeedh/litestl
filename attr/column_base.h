#pragma once

/* ColumnBase: the polymorphic interface every attribute column implements.
 * Dispatch is per column per operation; per-element hot loops use the typed
 * Column<T> they obtained once.
 *
 * ColumnBase must be the first base of every column class (UniquePtr base
 * conversion keeps the alloc block pointer). */

#include <cstdint>
#include <string_view>

#include "attr/mix.h"
#include "attr/paged_store.h"
#include "io/bytes.h"
#include "util/compiler_util.h"
#include "util/hash128.h"
#include "util/memory.h"
#include "util/span.h"

namespace litestl::attr {

/* Topo: written by topology operators only; group-level mix never touches it.
 * Temp, Derived: excluded from content hashes and serialized data (Derived is
 * recomputable). Bits 16..31 are free for users. */
enum class ColumnFlags : uint32_t { None = 0, Topo = 1, Temp = 2, Derived = 4 };
FlagOperators(ColumnFlags);

inline bool has_flag(ColumnFlags f, ColumnFlags bit)
{
  return (uint32_t(f) & uint32_t(bit)) != 0;
}

struct ReadLimits {
  uint32_t max_elements = 1u << 30;
  uint32_t max_columns = 1u << 16;
};

class ColumnBase;

/** Registry entry: one per column class. */
struct ColumnType {
  std::string_view key; /* points into storage owned by the registry */
  uint32_t value_size = 0;
  /* new empty column with the given default (value_size bytes) and page shift */
  util::UniquePtr<ColumnBase> (*create)(const uint8_t *default_bytes, int page_shift) = nullptr;
};

class ColumnBase {
public:
  virtual ~ColumnBase() = default;

  virtual const ColumnType &type() const = 0;
  virtual uint32_t size() const = 0;
  virtual void resize(uint32_t n) = 0;
  virtual uint32_t page_shift() const = 0;

  virtual void swap(uint32_t a, uint32_t b) = 0;
  /** old_to_new must be injective with values < new_size or kDropped;
   * unmapped new elements get the default (validated by the caller, e.g.
   * AttrGroup::permute). Uniform gathered pages stay Absent/Constant. */
  virtual void permute(util::span<const uint32_t> old_to_new, uint32_t new_size) = 0;
  static constexpr uint32_t kDropped = 0xffffffffu;

  virtual void set_default_range(uint32_t first, uint32_t n) = 0;
  virtual void set_default_list(util::span<const uint32_t> elems) = 0;

  /** Simultaneous mix within this column. Returns false (with *err) for an
   * unsupported policy; the column is then unchanged. */
  virtual bool mix(const ValidatedMixBatch &b, MixPolicy p, const MixContext &ctx, const char **err) = 0;
  /** Same, reading sources from @p src (must have the same ColumnType). */
  virtual bool mix_from(const ColumnBase &src,
                        const ValidatedMixBatch &b,
                        MixPolicy p,
                        const MixContext &ctx,
                        const char **err) = 0;

  /** Copy-on-write snapshot of this column. */
  virtual util::UniquePtr<ColumnBase> share() const = 0;
  /** New empty column of the same type, default, flags and page shift, resized to n. */
  virtual util::UniquePtr<ColumnBase> new_like(uint32_t n) const = 0;

  /* type-erased element access (journal capture etc.) */
  virtual uint32_t elem_size() const = 0;
  virtual void get_bytes(uint32_t i, uint8_t *out) const = 0;
  virtual void set_bytes(uint32_t i, const uint8_t *in) = 0;
  virtual void default_bytes(uint8_t *out) const = 0;

  /* type-erased page API */
  virtual uint32_t page_count() const = 0;
  virtual PageState page_state(uint32_t p) const = 0;
  virtual bool same_page_as(const ColumnBase &o, uint32_t p) const = 0;

  /** Feeds the column's canonical content into h: type key, flags, size,
   * default and the live element stream (independent of page size,
   * representation and history). */
  virtual void hash_into(hash::Hasher128 &h) const = 0;
  /** Canonical serialization (run-encoded element stream). */
  virtual void write(io::ByteWriter &w) const = 0;
  /** Reads data written by write() into this (already typed) column;
   * returns false on malformed input or limit violation (column unspecified). */
  virtual bool read_data(io::ByteReader &r, const ReadLimits &lim) = 0;

  /** Never-before-seen value after any mutation (process-wide counter). */
  virtual uint64_t version() const = 0;

  ColumnFlags flags() const
  {
    return flags_;
  }
  void set_flags(ColumnFlags f)
  {
    flags_ = f;
    on_flags_changed();
  }
  MixPolicy default_policy() const
  {
    return default_policy_;
  }
  void set_default_policy(MixPolicy p)
  {
    default_policy_ = p;
  }

  /** Sibling slots this column writes itself in Custom mixes (A4). */
  virtual util::span<const uint32_t> claimed_slots() const
  {
    return {};
  }

protected:
  /** Custom-policy hook (MixPolicy::Custom); return false if unsupported. */
  virtual bool mix_custom(const ValidatedMixBatch &, const ColumnBase * /*src or null*/, const MixContext &)
  {
    return false;
  }
  virtual void on_flags_changed() = 0;

  ColumnFlags flags_ = ColumnFlags::None;
  MixPolicy default_policy_ = MixPolicy::CopyFirst;
};

} // namespace litestl::attr
