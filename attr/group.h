#pragma once

/* AttrGroup: a named, insertion-ordered set of equally sized columns.
 *
 * Value semantics: copying a group shares every column copy-on-write (a
 * snapshot). Builtin slots: an owner registers its builtin columns first, in
 * a fixed order, and reaches them by constant slot index, so copies and moves
 * never leave cached pointers dangling.
 *
 * Canonical forms (content hash, serialization) list builtins in slot order,
 * then dynamic columns sorted by name, and exclude Temp/Derived column data,
 * so they don't depend on ensure()/remove() history. */

#include <string_view>

#include "attr/column.h"
#include "util/function.h"
#include "util/string.h"
#include "util/vector.h"

namespace litestl::attr {

class AttrGroup {
public:
  explicit AttrGroup(int page_shift = kDefaultPageShift);
  AttrGroup(const AttrGroup &b);
  AttrGroup &operator=(const AttrGroup &b);
  AttrGroup(AttrGroup &&b) noexcept;
  AttrGroup &operator=(AttrGroup &&b) noexcept;
  ~AttrGroup() = default;

  uint32_t size() const
  {
    return size_;
  }
  void resize(uint32_t n);
  int page_shift() const
  {
    return page_shift_;
  }

  uint32_t column_count() const
  {
    return uint32_t(cols_.size());
  }
  uint32_t builtin_count() const
  {
    return builtins_;
  }
  ColumnBase &column(uint32_t i)
  {
    return *cols_[int(i)].col;
  }
  const ColumnBase &column(uint32_t i) const
  {
    return *cols_[int(i)].col;
  }
  std::string_view name(uint32_t i) const
  {
    return std::string_view(cols_[int(i)].name.c_str());
  }
  /** Changes whenever columns are added, removed or replaced (by read()). */
  uint64_t schema_version() const
  {
    return schema_version_;
  }

  ColumnBase *find_any(std::string_view name);
  const ColumnBase *find_any(std::string_view name) const;
  int index_of(std::string_view name) const;

  template <typename Col> Col *find(std::string_view name)
  {
    ColumnBase *c = find_any(name);
    return c && &c->type() == &Col::static_type() ? static_cast<Col *>(c) : nullptr;
  }

  /** Returns the existing column if name, type, default and flags all match;
   * null if the name exists with a different type/default/flags. */
  template <typename Col>
  Col *ensure(std::string_view name, const typename Col::value_type &def = {}, ColumnFlags flags = {})
  {
    return static_cast<Col *>(ensure_impl(name, Col::static_type(), &def, flags, false));
  }

  /** Adds a builtin column; null once any dynamic column exists or the name
   * is taken. */
  template <typename Col>
  Col *add_builtin(std::string_view name, const typename Col::value_type &def = {}, ColumnFlags flags = {})
  {
    return static_cast<Col *>(ensure_impl(name, Col::static_type(), &def, flags, true));
  }

  template <typename Col> Col &builtin(uint32_t slot)
  {
#if LITESTL_ATTR_CHECKS
    if (slot >= builtins_ || &cols_[int(slot)].col->type() != &Col::static_type()) {
      fprintf(stderr, "litestl::attr: builtin slot %u type mismatch\n", slot);
      abort();
    }
#endif
    return static_cast<Col &>(*cols_[int(slot)].col);
  }
  template <typename Col> const Col &builtin(uint32_t slot) const
  {
    return const_cast<AttrGroup *>(this)->builtin<Col>(slot);
  }

  /** False for builtin columns or unknown names. */
  bool remove(std::string_view name);

  void swap(uint32_t a, uint32_t b);
  /** old_to_new: injective, values < new_size or ColumnBase::kDropped. */
  static bool validate_permutation(util::span<const uint32_t> old_to_new,
                                   uint32_t old_size,
                                   uint32_t new_size,
                                   const char **err);
  /** Validates, then permutes every column (serially; callers wanting
   * parallelism permute column(i) themselves after validate_permutation). */
  bool permute(util::span<const uint32_t> old_to_new, uint32_t new_size, const char **err = nullptr);
  void set_default_list(util::span<const uint32_t> elems);

  using PolicyFn = util::function_ref<MixPolicy(std::string_view name, uint32_t slot, const ColumnBase &col)>;

  /** Simultaneous mix over all non-Topo, unclaimed columns. policy_of may be
   * empty (column default policies are used). Validates once. */
  bool mix(const MixBatch &b, const PolicyFn *policy_of = nullptr, const void *op = nullptr, const char **err = nullptr);
  /** Mix with sources from @p src, matching columns by name and type;
   * columns this group lacks are created with new_like(). */
  bool mix_from(const AttrGroup &src,
                const MixBatch &b,
                const PolicyFn *policy_of = nullptr,
                const void *op = nullptr,
                const char **err = nullptr);

  void hash_into(hash::Hasher128 &h) const;
  hash::Hash128 content_hash() const;

  /** Column filter for partial hashes: return true to include the column.
   * Temp and Derived columns are excluded before the filter is consulted. */
  using ColumnFilter = util::function_ref<bool(std::string_view name, const ColumnBase &col)>;
  /** Canonical hash over the columns accepted by @p keep (same encoding as
   * hash_into, so a filter that keeps everything gives the same hash). */
  void hash_into(hash::Hasher128 &h, const ColumnFilter &keep) const;

  /** Version stamp of the whole group: changes, to a never-before-seen value,
   * after any mutation of any column or of the column set. (The maximum of
   * the column stamps and schema_version(): every mutation produces a stamp
   * newer than all stamps issued before it.) */
  uint64_t version() const;

  void write(io::ByteWriter &w) const;
  /** Reads a stream written by write(). The stream's builtin layout must
   * match this group's (names, keys, flags, defaults). Parses into a staging
   * group and replaces this group's contents only on success; existing
   * dynamic columns are dropped. */
  bool read(io::ByteReader &r, const ReadLimits &lim = {}, const char **err = nullptr);

private:
  struct Slot {
    util::string name;
    util::UniquePtr<ColumnBase> col;
  };

  ColumnBase *ensure_impl(std::string_view name,
                          const ColumnType &type,
                          const void *def,
                          ColumnFlags flags,
                          bool builtin);
  util::Vector<int> canonical_order() const;
  bool mix_common(const AttrGroup *src, const MixBatch &b, const PolicyFn *policy_of, const void *op, const char **err);

  util::Vector<Slot, 8> cols_;
  uint32_t builtins_ = 0;
  uint32_t size_ = 0;
  int page_shift_;
  uint64_t schema_version_;
};

} // namespace litestl::attr
