#include "group.h"
#include "registry.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace litestl::attr {

namespace {
constexpr uint32_t kFormatVersion = 1;

bool hashed(const ColumnBase &c)
{
  return !has_flag(c.flags(), ColumnFlags::Temp) && !has_flag(c.flags(), ColumnFlags::Derived);
}

bool same_default(const ColumnBase &a, const ColumnBase &b)
{
  if (a.elem_size() != b.elem_size()) {
    return false;
  }
  std::vector<uint8_t> x(a.elem_size()), y(b.elem_size());
  a.default_bytes(x.data());
  b.default_bytes(y.data());
  return x == y;
}
} // namespace

AttrGroup::AttrGroup(int page_shift) : page_shift_(page_shift), schema_version_(next_version()) {}

AttrGroup::AttrGroup(const AttrGroup &b)
    : builtins_(b.builtins_), size_(b.size_), page_shift_(b.page_shift_), schema_version_(b.schema_version_)
{
  for (int i = 0; i < b.cols_.size(); i++) {
    cols_.append(Slot{b.cols_[i].name, b.cols_[i].col->share()});
  }
}

AttrGroup &AttrGroup::operator=(const AttrGroup &b)
{
  if (this != &b) {
    AttrGroup tmp(b);
    *this = std::move(tmp);
  }
  return *this;
}

AttrGroup::AttrGroup(AttrGroup &&b) noexcept
    : cols_(std::move(b.cols_)),
      builtins_(b.builtins_),
      size_(b.size_),
      page_shift_(b.page_shift_),
      schema_version_(b.schema_version_)
{
  b.builtins_ = 0;
  b.size_ = 0;
  b.schema_version_ = next_version();
}

AttrGroup &AttrGroup::operator=(AttrGroup &&b) noexcept
{
  if (this != &b) {
    cols_ = std::move(b.cols_);
    builtins_ = b.builtins_;
    size_ = b.size_;
    page_shift_ = b.page_shift_;
    schema_version_ = b.schema_version_;
    b.builtins_ = 0;
    b.size_ = 0;
    b.schema_version_ = next_version();
  }
  return *this;
}

void AttrGroup::resize(uint32_t n)
{
  for (int i = 0; i < cols_.size(); i++) {
    cols_[i].col->resize(n);
  }
  size_ = n;
}

int AttrGroup::index_of(std::string_view name) const
{
  for (int i = 0; i < cols_.size(); i++) {
    if (std::string_view(cols_[i].name.c_str()) == name) {
      return i;
    }
  }
  return -1;
}

ColumnBase *AttrGroup::find_any(std::string_view name)
{
  const int i = index_of(name);
  return i < 0 ? nullptr : cols_[i].col.get();
}

const ColumnBase *AttrGroup::find_any(std::string_view name) const
{
  const int i = index_of(name);
  return i < 0 ? nullptr : cols_[i].col.get();
}

ColumnBase *AttrGroup::ensure_impl(
    std::string_view name, const ColumnType &type, const void *def, ColumnFlags flags, bool builtin)
{
  if (const int i = index_of(name); i >= 0) {
    ColumnBase &c = *cols_[i].col;
    if (builtin || &c.type() != &type || c.flags() != flags) {
      return nullptr;
    }
    std::vector<uint8_t> cur(c.elem_size());
    c.default_bytes(cur.data());
    return memcmp(cur.data(), def, cur.size()) == 0 ? &c : nullptr;
  }
  if (builtin && builtins_ != uint32_t(cols_.size())) {
    return nullptr;
  }
  util::UniquePtr<ColumnBase> col = type.create(static_cast<const uint8_t *>(def), page_shift_);
  col->set_flags(flags);
  col->resize(size_);
  ColumnBase *raw = col.get();
  std::string tmp(name);
  cols_.append(Slot{util::string(tmp.c_str()), std::move(col)});
  if (builtin) {
    builtins_++;
  }
  schema_version_ = next_version();
  return raw;
}

bool AttrGroup::remove(std::string_view name)
{
  const int i = index_of(name);
  if (i < 0 || uint32_t(i) < builtins_) {
    return false;
  }
  cols_.remove_at(i);
  schema_version_ = next_version();
  return true;
}

void AttrGroup::swap(uint32_t a, uint32_t b)
{
  for (int i = 0; i < cols_.size(); i++) {
    cols_[i].col->swap(a, b);
  }
}

bool AttrGroup::validate_permutation(util::span<const uint32_t> old_to_new,
                                     uint32_t old_size,
                                     uint32_t new_size,
                                     const char **err)
{
  if (old_to_new.size() != old_size) {
    if (err) {
      *err = "permute: map size must equal the group size";
    }
    return false;
  }
  std::vector<uint8_t> seen(new_size, 0);
  for (uint32_t v : old_to_new) {
    if (v == ColumnBase::kDropped) {
      continue;
    }
    if (v >= new_size || seen[v]) {
      if (err) {
        *err = v >= new_size ? "permute: target out of range" : "permute: map is not injective";
      }
      return false;
    }
    seen[v] = 1;
  }
  return true;
}

bool AttrGroup::permute(util::span<const uint32_t> old_to_new, uint32_t new_size, const char **err)
{
  if (!validate_permutation(old_to_new, size_, new_size, err)) {
    return false;
  }
  for (int i = 0; i < cols_.size(); i++) {
    cols_[i].col->permute(old_to_new, new_size);
  }
  size_ = new_size;
  return true;
}

void AttrGroup::set_default_list(util::span<const uint32_t> elems)
{
  for (int i = 0; i < cols_.size(); i++) {
    cols_[i].col->set_default_list(elems);
  }
}

bool AttrGroup::mix_common(
    const AttrGroup *src, const MixBatch &b, const PolicyFn *policy_of, const void *op, const char **err)
{
  ValidatedMixBatch vb;
  if (!ValidatedMixBatch::validate(b, size_, src ? src->size_ : size_, vb, err)) {
    return false;
  }
  if (src) {
    /* create columns this group lacks (before snapshotting) */
    for (int i = 0; i < src->cols_.size(); i++) {
      const ColumnBase &sc = *src->cols_[i].col;
      if (has_flag(sc.flags(), ColumnFlags::Topo)) {
        continue;
      }
      const std::string_view nm(src->cols_[i].name.c_str());
      if (!find_any(nm)) {
        std::string tmp(nm);
        cols_.append(Slot{util::string(tmp.c_str()), sc.new_like(size_)});
        schema_version_ = next_version();
      }
    }
  }
  /* policies and claimed slots */
  std::vector<MixPolicy> pol(size_t(cols_.size()));
  std::vector<uint8_t> claimed(size_t(cols_.size()), 0);
  bool any_custom = false;
  for (int i = 0; i < cols_.size(); i++) {
    const ColumnBase &c = *cols_[i].col;
    pol[size_t(i)] = policy_of ? (*policy_of)(std::string_view(cols_[i].name.c_str()), uint32_t(i), c) :
                                 c.default_policy();
    if (pol[size_t(i)] == MixPolicy::Custom) {
      any_custom = true;
      for (uint32_t s : c.claimed_slots()) {
        if (s < claimed.size() && int(s) != i) {
          claimed[s] = 1;
        }
      }
    }
  }
  /* Custom handlers read siblings from a pre-call snapshot */
  util::UniquePtr<AttrGroup> pre;
  if (any_custom) {
    pre = util::make_unique<AttrGroup>("attr::AttrGroup pre-mix", *this);
  }
  for (int i = 0; i < cols_.size(); i++) {
    ColumnBase &c = *cols_[i].col;
    if (has_flag(c.flags(), ColumnFlags::Topo) || claimed[size_t(i)]) {
      continue;
    }
    MixContext ctx{pre.get(), src, op, uint32_t(i)};
    bool ok;
    if (src) {
      const ColumnBase *sc = src->find_any(std::string_view(cols_[i].name.c_str()));
      if (!sc) {
        continue; /* nothing to mix from */
      }
      ok = c.mix_from(*sc, vb, pol[size_t(i)], ctx, err);
    }
    else {
      ok = c.mix(vb, pol[size_t(i)], ctx, err);
    }
    if (!ok) {
      return false;
    }
  }
  return true;
}

bool AttrGroup::mix(const MixBatch &b, const PolicyFn *policy_of, const void *op, const char **err)
{
  return mix_common(nullptr, b, policy_of, op, err);
}

bool AttrGroup::mix_from(
    const AttrGroup &src, const MixBatch &b, const PolicyFn *policy_of, const void *op, const char **err)
{
  return mix_common(&src, b, policy_of, op, err);
}

util::Vector<int> AttrGroup::canonical_order() const
{
  util::Vector<int> order;
  for (uint32_t i = 0; i < builtins_; i++) {
    order.append(int(i));
  }
  std::vector<int> dyn;
  for (int i = int(builtins_); i < cols_.size(); i++) {
    dyn.push_back(i);
  }
  std::sort(dyn.begin(), dyn.end(), [&](int a, int b) {
    return std::string_view(cols_[a].name.c_str()) < std::string_view(cols_[b].name.c_str());
  });
  for (int i : dyn) {
    order.append(i);
  }
  return order;
}

void AttrGroup::hash_into(hash::Hasher128 &h) const
{
  hash_into(h, [](std::string_view, const ColumnBase &) { return true; });
}

void AttrGroup::hash_into(hash::Hasher128 &h, const ColumnFilter &keep) const
{
  hash_into_impl(h, keep, nullptr);
}

void AttrGroup::hash_into(hash::Hasher128 &h, const ColumnFilter &keep, const RefRemap &map) const
{
  hash_into_impl(h, keep, &map);
}

void AttrGroup::hash_into_impl(hash::Hasher128 &h, const ColumnFilter &keep, const RefRemap *map) const
{
  const util::Vector<int> order = canonical_order();
  auto included = [&](int i) {
    const ColumnBase &c = *cols_[i].col;
    return hashed(c) && keep(std::string_view(cols_[i].name.c_str()), c);
  };
  h.u32(size_);
  h.u32(builtins_);
  uint32_t n = 0;
  for (int i : order) {
    n += included(i) ? 1 : 0;
  }
  h.u32(n);
  for (int i : order) {
    if (!included(i)) {
      continue;
    }
    h.str(std::string_view(cols_[i].name.c_str()));
    if (map && cols_[i].col->holds_refs()) {
      cols_[i].col->hash_into_mapped(h, *map);
    }
    else {
      cols_[i].col->hash_into(h);
    }
  }
}

uint64_t AttrGroup::version() const
{
  uint64_t v = schema_version_;
  for (const Slot &s : cols_) {
    v = std::max(v, s.col->version());
  }
  return v;
}

hash::Hash128 AttrGroup::content_hash() const
{
  hash::Hasher128 h(hash::DomainTag::Content);
  hash_into(h);
  return h.finish();
}

void AttrGroup::write(io::ByteWriter &w) const
{
  const util::Vector<int> order = canonical_order();
  w.u32(kFormatVersion);
  w.u32(size_);
  w.u32(builtins_);
  std::vector<uint8_t> def;
  for (uint32_t i = 0; i < builtins_; i++) {
    const ColumnBase &c = *cols_[int(i)].col;
    w.str(std::string_view(cols_[int(i)].name.c_str()));
    w.str(c.type().key);
    w.u32(uint32_t(c.flags()));
    def.resize(c.elem_size());
    c.default_bytes(def.data());
    w.put(def.data(), def.size());
    w.u8(hashed(c) ? 1 : 0);
    if (hashed(c)) {
      c.write(w);
    }
  }
  uint32_t ndyn = 0;
  for (int i : order) {
    ndyn += (uint32_t(i) >= builtins_ && hashed(*cols_[i].col)) ? 1 : 0;
  }
  w.u32(ndyn);
  for (int i : order) {
    const ColumnBase &c = *cols_[i].col;
    if (uint32_t(i) < builtins_ || !hashed(c)) {
      continue;
    }
    w.str(std::string_view(cols_[i].name.c_str()));
    w.str(c.type().key);
    def.resize(c.elem_size());
    c.default_bytes(def.data());
    w.put(def.data(), def.size());
    c.write(w);
  }
  const hash::Hash128 h = content_hash();
  w.u64(h.lo);
  w.u64(h.hi);
}

bool AttrGroup::migrate_from(const AttrGroup &src, const ColumnFilter &keep, const char **err)
{
  auto fail = [&](const char *msg) {
    if (err) {
      *err = msg;
    }
    return false;
  };
  for (uint32_t i = 0; i < src.column_count(); i++) {
    const ColumnBase &c = src.column(i);
    if (!keep(src.name(i), c)) {
      continue;
    }
    if (c.page_shift() != uint32_t(page_shift_)) {
      return fail("migrate: page size differs");
    }
    const int j = index_of(src.name(i));
    if (j < 0) {
      if (i < src.builtins_) {
        return fail("migrate: a builtin has no counterpart");
      }
      continue;
    }
    const ColumnBase &mine = *cols_[j].col;
    std::vector<uint8_t> da(c.elem_size()), db(mine.elem_size());
    c.default_bytes(da.data());
    mine.default_bytes(db.data());
    if (&mine.type() != &c.type() || mine.flags() != c.flags() || da != db ||
        (i < src.builtins_) != (uint32_t(j) < builtins_))
    {
      return fail("migrate: column differs from its counterpart");
    }
  }
  resize(src.size());
  for (uint32_t i = 0; i < src.column_count(); i++) {
    const ColumnBase &c = src.column(i);
    if (!keep(src.name(i), c)) {
      continue;
    }
    const int j = index_of(src.name(i));
    if (j >= 0) {
      cols_[j].col = c.share();
    }
    else {
      std::string tmp(src.name(i));
      cols_.append(Slot{util::string(tmp.c_str()), c.share()});
    }
  }
  schema_version_ = next_version();
  return true;
}

bool AttrGroup::read(io::ByteReader &r, const ReadLimits &lim, const char **err)
{
  auto fail = [&](const char *msg) {
    if (err) {
      *err = msg;
    }
    return false;
  };
  if (r.u32() != kFormatVersion) {
    return fail("read: unsupported format version");
  }
  const uint32_t size = r.u32();
  const uint32_t nb = r.u32();
  if (r.failed() || size > lim.max_elements) {
    return fail("read: size exceeds limits");
  }
  if (nb != builtins_) {
    return fail("read: builtin layout mismatch (count)");
  }
  AttrGroup staging(page_shift_);
  staging.size_ = size;
  std::vector<uint8_t> def;
  for (uint32_t i = 0; i < nb; i++) {
    const ColumnBase &mine = *cols_[int(i)].col;
    const std::string_view nm = r.str();
    const std::string_view key = r.str();
    const uint32_t flags = r.u32();
    def.resize(mine.elem_size());
    r.get(def.data(), def.size());
    const uint8_t has_data = r.u8();
    std::vector<uint8_t> mydef(mine.elem_size());
    mine.default_bytes(mydef.data());
    if (r.failed() || nm != std::string_view(cols_[int(i)].name.c_str()) || key != mine.type().key ||
        flags != uint32_t(mine.flags()) || def != mydef || has_data != (hashed(mine) ? 1 : 0))
    {
      return fail("read: builtin layout mismatch");
    }
    util::UniquePtr<ColumnBase> col = mine.new_like(0);
    if (has_data) {
      if (!col->read_data(r, lim) || col->size() != size) {
        return fail("read: malformed builtin column data");
      }
    }
    else {
      col->resize(size);
    }
    std::string tmp(nm);
    staging.cols_.append(Slot{util::string(tmp.c_str()), std::move(col)});
    staging.builtins_++;
  }
  const uint32_t ndyn = r.u32();
  if (r.failed() || ndyn > lim.max_columns) {
    return fail("read: too many columns");
  }
  for (uint32_t i = 0; i < ndyn; i++) {
    const std::string_view nm = r.str();
    const std::string_view key = r.str();
    if (r.failed()) {
      return fail("read: truncated column header");
    }
    const ColumnType *t = ColumnTypeRegistry::global().find(key);
    if (!t) {
      return fail("read: unknown column type key");
    }
    def.resize(t->value_size);
    r.get(def.data(), def.size());
    if (r.failed() || staging.index_of(nm) >= 0) {
      return fail("read: truncated or duplicate column");
    }
    util::UniquePtr<ColumnBase> col = t->create(def.data(), page_shift_);
    if (!col->read_data(r, lim) || col->size() != size) {
      return fail("read: malformed column data");
    }
    std::string tmp(nm);
    staging.cols_.append(Slot{util::string(tmp.c_str()), std::move(col)});
  }
  hash::Hash128 h;
  h.lo = r.u64();
  h.hi = r.u64();
  if (r.failed() || (lim.verify_hash && h != staging.content_hash())) {
    return fail("read: content hash mismatch (corrupt stream)");
  }
  staging.schema_version_ = next_version();
  *this = std::move(staging);
  return true;
}

} // namespace litestl::attr
