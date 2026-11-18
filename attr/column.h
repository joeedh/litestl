#pragma once

/* Column<T, Ops>: the typed attribute column over PagedStore<T>. */

#include <algorithm>
#include <cstring>
#include <string>

#include "attr/column_base.h"
#include "attr/registry.h"
#include "attr/type_key.h"
#include "attr/value_ops.h"
#include "util/task.h"

namespace litestl::attr {

/* Serialized run kinds (format constants). */
enum class RunKind : uint8_t { Default = 0, Constant = 1, Dense = 2 };
/* Runs of >= kRunMin bitwise-equal values are emitted as Default/Constant. */
constexpr uint32_t kRunMin = 8;

namespace detail {
template <typename T, typename Ops> std::string column_key()
{
  if constexpr (std::is_same_v<Ops, ValueOps<T>>) {
    return ValueKey<T>::key();
  }
  else {
    return Ops::key(); /* custom ops must declare their own key */
  }
}
} // namespace detail

template <ColumnValue T, typename Ops = ValueOps<T>> class Column : public ColumnBase {
  static_assert(!std::is_same_v<T, bool>, "use BoolColumn for bool columns");

public:
  using value_type = T;
  using ops_type = Ops;
  PagedStore<T> store;

  explicit Column(const T &default_value = T{}, int page_shift = kDefaultPageShift)
      : store(default_value, page_shift)
  {
    default_policy_ = Ops::default_policy;
  }

  static const ColumnType &static_type()
  {
    static const ColumnType *t = ColumnTypeRegistry::global().add_or_die<Column>();
    return *t;
  }

  static util::UniquePtr<ColumnBase> create(const uint8_t *default_bytes, int page_shift)
  {
    T def;
    memcpy(static_cast<void *>(&def), default_bytes, sizeof(T));
    return util::UniquePtr<ColumnBase>(util::make_unique<Column>("attr::Column", def, page_shift));
  }

  static std::string type_key()
  {
    return detail::column_key<T, Ops>();
  }

  /* ---- typed access ---- */
  const T &operator[](uint32_t i) const
  {
    return store.get(i);
  }
  const T &get(uint32_t i) const
  {
    return store.get(i);
  }
  T &write(uint32_t i)
  {
    return store.write(i);
  }
  void set(uint32_t i, const T &v)
  {
    store.set(i, v);
  }
  void fill(uint32_t first, uint32_t n, const T &v)
  {
    store.fill(first, n, v);
  }

  /** f(span<const T> live, uint32_t base) for each page, in order. */
  template <typename F> void for_each_page(F &&f) const
  {
    util::Vector<T> scratch;
    for (uint32_t p = 0; p < store.page_count(); p++) {
      f(store.page_span(p, scratch), p << store.page_shift());
    }
  }

  /** Parallel writes over [first, first+n): pages are made unique first, then
   * f(span<T> page_part, uint32_t base) runs once per touched page, possibly
   * on several threads. Only the given span may be written inside f. */
  template <typename F> void parallel_write(uint32_t first, uint32_t n, F &&f)
  {
    if (n == 0) {
      return;
    }
    store.prepare_write(first, n);
    const uint32_t p0 = first >> store.page_shift();
    const uint32_t p1 = (first + n - 1) >> store.page_shift();
    util::Vector<util::span<T>> spans;
    for (uint32_t p = p0; p <= p1; p++) {
      spans.append(store.page_write(p));
    }
    const uint32_t shift = store.page_shift();
    task::parallel_for(
        util::IndexRange(int(p1 - p0 + 1)),
        [&](util::IndexRange r) {
          for (int k : r) {
            const uint32_t p = p0 + uint32_t(k);
            const uint32_t base = p << shift;
            const uint32_t lo = std::max(first, base) - base;
            const uint32_t hi = std::min(first + n, base + uint32_t(spans[k].size())) - base;
            f(spans[k].subspan(lo, hi - lo), base + lo);
          }
        },
        1);
    store.mark_modified();
  }

  /* ---- ColumnBase ---- */
  const ColumnType &type() const override
  {
    return static_type();
  }
  uint32_t size() const override
  {
    return store.size();
  }
  void resize(uint32_t n) override
  {
    store.resize(n);
  }
  uint32_t page_shift() const override
  {
    return store.page_shift();
  }

  void swap(uint32_t a, uint32_t b) override
  {
    if (a == b) {
      return;
    }
    const T va = store.get(a), vb = store.get(b);
    store.set(a, vb);
    store.set(b, va);
  }

  void permute(util::span<const uint32_t> old_to_new, uint32_t new_size) override
  {
    PagedStore<T> out(store.default_value(), int(store.page_shift()));
    out.resize(new_size);
    /* inverse map, then gather page by page so uniform pages stay uniform */
    util::Vector<uint32_t> new_to_old;
    new_to_old.resize(int(new_size));
    for (uint32_t i = 0; i < new_size; i++) {
      new_to_old[i] = kDropped;
    }
    for (size_t o = 0; o < old_to_new.size(); o++) {
      if (old_to_new[o] != kDropped) {
        new_to_old[old_to_new[o]] = uint32_t(o);
      }
    }
    for (uint32_t p = 0; p < out.page_count(); p++) {
      const uint32_t base = p << out.page_shift();
      const uint32_t live = out.page_live_count(p);
      bool uniform = true;
      T first_v = new_to_old[base] == kDropped ? store.default_value() : store.get(new_to_old[base]);
      for (uint32_t k = 1; k < live && uniform; k++) {
        const uint32_t o = new_to_old[base + k];
        const T &v = o == kDropped ? store.default_value() : store.get(o);
        uniform = bit_equal(v, first_v);
      }
      if (uniform) {
        out.fill(base, live, first_v);
        continue;
      }
      util::span<T> dst = out.page_write(p);
      for (uint32_t k = 0; k < live; k++) {
        const uint32_t o = new_to_old[base + k];
        dst[k] = o == kDropped ? store.default_value() : store.get(o);
      }
    }
    store = std::move(out);
  }

  void set_default_range(uint32_t first, uint32_t n) override
  {
    store.set_default_range(first, n);
  }
  void set_default_list(util::span<const uint32_t> elems) override
  {
    for (uint32_t i : elems) {
      store.set(i, store.default_value());
    }
  }

  bool mix(const ValidatedMixBatch &b, MixPolicy p, const MixContext &ctx, const char **err) override
  {
    return mix_impl(*this, b, p, ctx, err);
  }

  bool mix_from(const ColumnBase &src,
                const ValidatedMixBatch &b,
                MixPolicy p,
                const MixContext &ctx,
                const char **err) override
  {
    if (&src.type() != &type()) {
      if (err) {
        *err = "mix_from: source column has a different type";
      }
      return false;
    }
    return mix_impl(static_cast<const Column &>(src), b, p, ctx, err);
  }

  util::UniquePtr<ColumnBase> share() const override
  {
    auto c = util::make_unique<Column>("attr::Column", store.default_value(), int(store.page_shift()));
    c->store = store.share();
    c->flags_ = flags_;
    c->default_policy_ = default_policy_;
    return util::UniquePtr<ColumnBase>(std::move(c));
  }

  util::UniquePtr<ColumnBase> new_like(uint32_t n) const override
  {
    auto c = util::make_unique<Column>("attr::Column", store.default_value(), int(store.page_shift()));
    c->flags_ = flags_;
    c->default_policy_ = default_policy_;
    c->store.resize(n);
    return util::UniquePtr<ColumnBase>(std::move(c));
  }

  uint32_t elem_size() const override
  {
    return sizeof(T);
  }
  void get_bytes(uint32_t i, uint8_t *out) const override
  {
    memcpy(out, static_cast<const void *>(&store.get(i)), sizeof(T));
  }
  void set_bytes(uint32_t i, const uint8_t *in) override
  {
    T v;
    memcpy(static_cast<void *>(&v), in, sizeof(T));
    store.set(i, v);
  }
  void default_bytes(uint8_t *out) const override
  {
    memcpy(out, static_cast<const void *>(&store.default_value()), sizeof(T));
  }

  uint32_t page_count() const override
  {
    return store.page_count();
  }
  PageState page_state(uint32_t p) const override
  {
    return store.page_state(p);
  }
  bool same_page_as(const ColumnBase &o, uint32_t p) const override
  {
    return &o.type() == &type() && store.same_page_as(static_cast<const Column &>(o).store, p);
  }

  void hash_into(hash::Hasher128 &h) const override
  {
    h.str(type().key);
    h.u32(uint32_t(flags_));
    h.u32(store.size());
    h.raw_bytes(&store.default_value(), sizeof(T));
    for (uint32_t p = 0; p < store.page_count(); p++) {
      const uint32_t live = store.page_live_count(p);
      if (const T *u = store.page_uniform(p)) {
        for (uint32_t k = 0; k < live; k++) {
          h.raw_bytes(u, sizeof(T));
        }
      }
      else {
        const util::span<const uint8_t> bytes = store.page_bytes(p);
        h.raw_bytes(bytes.data(), bytes.size());
      }
    }
  }

  void write(io::ByteWriter &w) const override
  {
    w.u32(uint32_t(flags_));
    w.u32(store.size());
    w.put(&store.default_value(), sizeof(T));
    RunWriter rw(w, store.default_value());
    for (uint32_t p = 0; p < store.page_count(); p++) {
      const uint32_t live = store.page_live_count(p);
      if (const T *u = store.page_uniform(p)) {
        rw.feed(*u, live);
      }
      else {
        util::Vector<T> scratch;
        for (const T &v : store.page_span(p, scratch)) {
          rw.feed(v, 1);
        }
      }
    }
    rw.finish();
  }

  bool read_data(io::ByteReader &r, const ReadLimits &lim) override
  {
    const uint32_t flags = r.u32();
    const uint32_t size = r.u32();
    T def;
    r.get(&def, sizeof(T));
    if (r.failed() || size > lim.max_elements || !bit_equal(def, store.default_value())) {
      return false;
    }
    flags_ = ColumnFlags(flags);
    PagedStore<T> out(store.default_value(), int(store.page_shift()));
    out.resize(size);
    const uint32_t nruns = r.u32();
    uint64_t pos = 0;
    for (uint32_t k = 0; k < nruns && !r.failed(); k++) {
      const uint8_t kind = r.u8();
      const uint32_t count = r.u32();
      if (r.failed() || count == 0 || pos + count > size) {
        return false;
      }
      if (kind == uint8_t(RunKind::Default)) {
        /* already default */
      }
      else if (kind == uint8_t(RunKind::Constant)) {
        T v;
        r.get(&v, sizeof(T));
        out.fill(uint32_t(pos), count, v);
      }
      else if (kind == uint8_t(RunKind::Dense)) {
        if (uint64_t(count) * sizeof(T) > r.remaining()) {
          return false;
        }
        for (uint32_t i = 0; i < count; i++) {
          T v;
          r.get(&v, sizeof(T));
          out.set(uint32_t(pos) + i, v);
        }
      }
      else {
        return false;
      }
      pos += count;
    }
    if (r.failed() || pos != size) {
      return false;
    }
    store = std::move(out);
    return true;
  }

  uint64_t version() const override
  {
    return store.version();
  }

protected:
  void on_flags_changed() override
  {
    store.mark_modified();
  }

private:
  /* Canonical run encoder over the element stream (A11). */
  class RunWriter {
    io::ByteWriter &w_;
    io::ByteWriter body_;
    const T &def_;
    util::Vector<T> dense_;
    T run_v_{};
    uint64_t run_n_ = 0;
    uint32_t nruns_ = 0;

    void flush_dense()
    {
      if (dense_.size() == 0) {
        return;
      }
      body_.u8(uint8_t(RunKind::Dense));
      body_.u32(uint32_t(dense_.size()));
      body_.put(dense_.data(), size_t(dense_.size()) * sizeof(T));
      dense_.clear();
      nruns_++;
    }
    void close_run()
    {
      if (run_n_ == 0) {
        return;
      }
      if (run_n_ >= kRunMin) {
        flush_dense();
        if (bit_equal(run_v_, def_)) {
          body_.u8(uint8_t(RunKind::Default));
          body_.u32(uint32_t(run_n_));
        }
        else {
          body_.u8(uint8_t(RunKind::Constant));
          body_.u32(uint32_t(run_n_));
          body_.put(&run_v_, sizeof(T));
        }
        nruns_++;
      }
      else {
        for (uint64_t i = 0; i < run_n_; i++) {
          dense_.append(run_v_);
        }
      }
      run_n_ = 0;
    }

  public:
    RunWriter(io::ByteWriter &w, const T &def) : w_(w), def_(def) {}
    void feed(const T &v, uint32_t n)
    {
      if (run_n_ && bit_equal(v, run_v_)) {
        run_n_ += n;
        return;
      }
      close_run();
      run_v_ = v;
      run_n_ = n;
    }
    void finish()
    {
      close_run();
      flush_dense();
      w_.u32(nruns_);
      w_.put(body_.data(), body_.size());
    }
  };

  bool mix_impl(const Column &src, const ValidatedMixBatch &vb, MixPolicy p, const MixContext &ctx, const char **err)
  {
    if (p == MixPolicy::Skip) {
      return true;
    }
    if (p == MixPolicy::Custom) {
      if (!mix_custom(vb, &src == this ? nullptr : &src, ctx)) {
        if (err) {
          *err = "mix: column has no Custom mix";
        }
        return false;
      }
      store.mark_modified();
      return true;
    }
    if (!Ops::supports(p)) {
      if (err) {
        *err = "mix: policy not supported for this value type";
      }
      return false;
    }
    const MixBatch &b = vb.batch();
    const size_t nt = b.targets.size();
    /* gather (simultaneous semantics), then scatter */
    util::Vector<T, 16> results;
    results.resize(int(nt));
    util::Vector<T, 16> vals;
    for (size_t k = 0; k < nt; k++) {
      const uint32_t s0 = b.offsets[k], s1 = b.offsets[k + 1];
      const int n = int(s1 - s0);
      if (n == 0 || p == MixPolicy::SetDefault) {
        results[int(k)] = store.default_value();
        continue;
      }
      if (p == MixPolicy::CopyFirst) {
        results[int(k)] = src.store.get(b.sources[s0]);
        continue;
      }
      vals.resize(n);
      for (int i = 0; i < n; i++) {
        vals[i] = src.store.get(b.sources[s0 + uint32_t(i)]);
      }
      const double *w = b.weights.empty() ? nullptr : b.weights.data() + s0;
      results[int(k)] = Ops::combine(p, vals.data(), w, n);
    }
    for (size_t k = 0; k < nt; k++) {
      store.set(b.targets[k], results[int(k)]);
    }
    store.mark_modified();
    return true;
  }
};

} // namespace litestl::attr
