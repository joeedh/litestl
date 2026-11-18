#include "attr/registry.h"
#include "attr/bool_column.h"
#include "attr/column.h"

namespace litestl::attr {

namespace {
template <typename E, int... Ns> void add_vecs(ColumnTypeRegistry &r)
{
  (r.add_or_die<Column<math::Vec<E, Ns>>>(), ...);
}

void register_builtins(ColumnTypeRegistry &r)
{
  r.add_or_die<Column<int8_t>>();
  r.add_or_die<Column<uint8_t>>();
  r.add_or_die<Column<int16_t>>();
  r.add_or_die<Column<uint16_t>>();
  r.add_or_die<Column<int32_t>>();
  r.add_or_die<Column<uint32_t>>();
  r.add_or_die<Column<int64_t>>();
  r.add_or_die<Column<uint64_t>>();
  r.add_or_die<Column<float>>();
  r.add_or_die<Column<double>>();
  r.add_or_die<Column<hash::Hash128>>();
  r.add_or_die<BoolColumn>();
  add_vecs<float, 2, 3, 4>(r);
  add_vecs<double, 2, 3, 4>(r);
  add_vecs<int32_t, 2, 3, 4>(r);
  add_vecs<uint32_t, 2, 3, 4>(r);
}
} // namespace

ColumnTypeRegistry &ColumnTypeRegistry::global()
{
  static ColumnTypeRegistry *r = [] {
    auto *g = new ColumnTypeRegistry();
    register_builtins(*g);
    return g;
  }();
  return *r;
}

const ColumnType *ColumnTypeRegistry::add_impl(std::string key,
                                               uint32_t value_size,
                                               util::UniquePtr<ColumnBase> (*create)(const uint8_t *, int),
                                               const void *tag,
                                               const char **err)
{
  std::lock_guard lock(mutex_);
  auto it = entries_.find(key);
  if (it != entries_.end()) {
    if (it->second->tag == tag) {
      return &it->second->type;
    }
    if (err) {
      *err = "a different column class already owns this type key";
    }
    return nullptr;
  }
  Entry *e = new Entry{std::move(key), ColumnType{}, tag};
  e->type.key = e->key;
  e->type.value_size = value_size;
  e->type.create = create;
  entries_.emplace(e->key, e);
  return &e->type;
}

const ColumnType *ColumnTypeRegistry::find(std::string_view key) const
{
  std::lock_guard lock(mutex_);
  auto it = entries_.find(key);
  return it == entries_.end() ? nullptr : &it->second->type;
}

size_t ColumnTypeRegistry::size() const
{
  std::lock_guard lock(mutex_);
  return entries_.size();
}

} // namespace litestl::attr
