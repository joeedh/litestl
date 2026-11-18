/* Model-based tests for litestl::attr columns: random operation sequences are
 * applied to a Column and to a std::vector model with an independent mix
 * implementation. After every checked step, contents, content hash and
 * serialized bytes are compared against independent reference encoders over
 * the model, and every live snapshot against its frozen model copy. Small
 * page shifts make every sequence cross many pages, tails and state changes. */

#include "litestl/attr/bool_column.h"
#include "litestl/attr/column.h"
#include "litestl/attr/registry.h"

#include "splitmix64.h"
#include "test_harness.h"

#include <cinttypes>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

test_init;

using namespace litestl;
using namespace litestl::attr;
using test::SplitMix64;

using F8 = std::array<float, 8>;

/* ---------------- value generation ---------------- */

template <typename T> struct Gen;

template <> struct Gen<float> {
  static float make(SplitMix64 &r)
  {
    switch (r.below(8)) {
      case 0:
        return 0.0f;
      case 1:
        return -0.0f;
      case 2:
        return std::bit_cast<float>(uint32_t(0x7fc00000u | (r.next() & 0xff))); /* NaN payloads */
      case 3:
        return std::bit_cast<float>(uint32_t(r.below(64) + 1)); /* denormal */
      case 4:
      case 5:
        return float(r.below(3)); /* runs of repeated values */
      default:
        return float(r.unit() * 200.0 - 100.0);
    }
  }
};
template <> struct Gen<double> {
  static double make(SplitMix64 &r)
  {
    switch (r.below(7)) {
      case 0:
        return -0.0;
      case 1:
        return std::bit_cast<double>(uint64_t(0x7ff8000000000000ull | (r.next() & 0xffff)));
      case 2:
        return std::bit_cast<double>(uint64_t(r.below(64) + 1));
      case 3:
      case 4:
        return double(r.below(3));
      default:
        return r.unit() * 2e6 - 1e6;
    }
  }
};
template <> struct Gen<int32_t> {
  static int32_t make(SplitMix64 &r)
  {
    switch (r.below(4)) {
      case 0:
        return int32_t(r.below(3));
      case 1:
        return r.coin() ? INT32_MAX : INT32_MIN;
      default:
        return int32_t(r.next());
    }
  }
};
template <> struct Gen<uint64_t> {
  static uint64_t make(SplitMix64 &r)
  {
    return r.below(3) == 0 ? r.below(3) : r.next();
  }
};
template <> struct Gen<uint8_t> { /* BoolColumn */
  static uint8_t make(SplitMix64 &r)
  {
    return uint8_t(r.coin());
  }
};
template <> struct Gen<hash::Hash128> {
  static hash::Hash128 make(SplitMix64 &r)
  {
    return r.below(3) == 0 ? hash::Hash128{r.below(2), 0} : hash::Hash128{r.next(), r.next()};
  }
};
template <> struct Gen<math::double3> {
  static math::double3 make(SplitMix64 &r)
  {
    return math::double3(Gen<double>::make(r), Gen<double>::make(r), Gen<double>::make(r));
  }
};
template <> struct Gen<F8> {
  static F8 make(SplitMix64 &r)
  {
    F8 v;
    const bool uniform = r.below(3) == 0;
    for (auto &x : v) {
      x = uniform ? 1.0f : Gen<float>::make(r);
    }
    return v;
  }
};

/* ---------------- independent reference mix ---------------- */

template <typename T> constexpr bool kFloatLike = FloatLike<T>;
template <typename T> constexpr bool kIntLike = IntegralLike<T>;

template <typename T> static T ref_combine(MixPolicy p, const std::vector<T> &src, const std::vector<double> &w)
{
  if constexpr (!(kFloatLike<T> || kIntLike<T>)) {
    return src[0]; /* never reached: only numeric families support these policies */
  }
  else {
  using E = detail::elem_t<T>;
  constexpr int N = detail::Components<T>::n;
  auto comp = [&](const T &v, int c) {
    E e[N];
    memcpy(static_cast<void *>(e), static_cast<const void *>(&v), sizeof(T));
    return e[c];
  };
  E out[N];
  for (int c = 0; c < N; c++) {
    if (p == MixPolicy::Sum) {
      if constexpr (kFloatLike<T>) {
        double acc = 0;
        for (auto &s : src) {
          acc += double(comp(s, c));
        }
        out[c] = detail::canonical_nan_if(E(acc));
      }
      else {
        using U = std::make_unsigned_t<E>;
        U acc = 0;
        for (auto &s : src) {
          acc = U(acc + U(comp(s, c)));
        }
        out[c] = E(acc);
      }
      continue;
    }
    double wsum = 0, acc = 0;
    for (size_t i = 0; i < src.size(); i++) {
      const double wi = w.empty() ? 1.0 : w[i];
      wsum += wi;
      acc += wi * double(comp(src[i], c));
    }
    if (p == MixPolicy::WeightedMean) {
      if (wsum == 0.0) {
        return src[0];
      }
      acc /= wsum;
    }
    if constexpr (kFloatLike<T>) {
      out[c] = detail::canonical_nan_if(E(acc));
    }
    else {
      if (acc != acc) {
        out[c] = 0;
      }
      else {
        const double rr = std::round(acc);
        out[c] = rr <= double(std::numeric_limits<E>::lowest()) ? std::numeric_limits<E>::lowest() :
                 rr >= double(std::numeric_limits<E>::max())    ? std::numeric_limits<E>::max() :
                                                                  E(rr);
      }
    }
  }
  T r;
  memcpy(static_cast<void *>(&r), static_cast<const void *>(out), sizeof(T));
  return r;
  }
}

/* ---------------- reference encoders over the model ---------------- */

template <typename T> static bool beq(const T &a, const T &b)
{
  return memcmp(&a, &b, sizeof(T)) == 0;
}

template <typename T> struct Model {
  std::vector<T> v;
  T def{};
  uint32_t flags = 0;
};

template <typename Col> static hash::Hash128 ref_hash(const Model<typename Col::value_type> &m)
{
  hash::Hasher128 h(hash::DomainTag::Content);
  h.str(Col::static_type().key);
  h.u32(m.flags);
  h.u32(uint32_t(m.v.size()));
  h.raw_bytes(&m.def, sizeof(m.def));
  for (auto &x : m.v) {
    h.raw_bytes(&x, sizeof(x));
  }
  return h.finish();
}

template <typename T> static std::vector<uint8_t> ref_bytes(const Model<T> &m)
{
  std::vector<uint8_t> out;
  auto put = [&](const void *p, size_t n) {
    const uint8_t *b = static_cast<const uint8_t *>(p);
    out.insert(out.end(), b, b + n);
  };
  auto u32 = [&](uint32_t x) { put(&x, 4); };
  u32(m.flags);
  u32(uint32_t(m.v.size()));
  put(&m.def, sizeof(T));
  std::vector<uint8_t> body;
  uint32_t nruns = 0;
  std::vector<T> dense;
  auto bput = [&](const void *p, size_t n) {
    const uint8_t *b = static_cast<const uint8_t *>(p);
    body.insert(body.end(), b, b + n);
  };
  auto flush_dense = [&] {
    if (dense.empty()) {
      return;
    }
    const uint8_t k = 2;
    const uint32_t n = uint32_t(dense.size());
    bput(&k, 1);
    bput(&n, 4);
    bput(dense.data(), dense.size() * sizeof(T));
    dense.clear();
    nruns++;
  };
  size_t i = 0;
  while (i < m.v.size()) {
    size_t j = i + 1;
    while (j < m.v.size() && beq(m.v[j], m.v[i])) {
      j++;
    }
    const uint32_t n = uint32_t(j - i);
    if (n >= kRunMin) {
      flush_dense();
      const uint8_t k = beq(m.v[i], m.def) ? 0 : 1;
      bput(&k, 1);
      bput(&n, 4);
      if (k == 1) {
        bput(&m.v[i], sizeof(T));
      }
      nruns++;
    }
    else {
      for (size_t q = i; q < j; q++) {
        dense.push_back(m.v[q]);
      }
    }
    i = j;
  }
  flush_dense();
  u32(nruns);
  out.insert(out.end(), body.begin(), body.end());
  return out;
}

/* ---------------- the model suite ---------------- */

template <typename Col> struct Snapshot {
  util::UniquePtr<ColumnBase> col;
  Model<typename Col::value_type> model;
};

template <typename Col>
static bool check(const char *name, const Col &c, const Model<typename Col::value_type> &m, int64_t step, int shift)
{
  bool ok = c.size() == m.v.size();
  size_t bad = 0;
  for (size_t i = 0; ok && i < m.v.size(); i++) {
    ok = beq(c.get(uint32_t(i)), m.v[i]);
    bad = i;
  }
  if (!ok) {
    fprintf(stderr,
            "[%s shift %d] contents diverge at step %" PRId64 " (size %u vs %zu, first bad index %zu, page %zu state %d)\n",
            name,
            shift,
            step,
            c.size(),
            m.v.size(),
            bad,
            bad >> shift,
            c.size() == m.v.size() ? int(c.page_state(uint32_t(bad >> shift))) : -1);
    if (c.size() == m.v.size()) {
      const uint8_t *a = reinterpret_cast<const uint8_t *>(&c.get(uint32_t(bad)));
      const uint8_t *b = reinterpret_cast<const uint8_t *>(&m.v[bad]);
      fprintf(stderr, "  column bytes:");
      for (size_t k = 0; k < std::min<size_t>(sizeof(m.v[0]), 16); k++) {
        fprintf(stderr, " %02x", a[k]);
      }
      fprintf(stderr, "\n  model  bytes:");
      for (size_t k = 0; k < std::min<size_t>(sizeof(m.v[0]), 16); k++) {
        fprintf(stderr, " %02x", b[k]);
      }
      fprintf(stderr, "\n");
    }
    return false;
  }
  hash::Hasher128 h(hash::DomainTag::Content);
  c.hash_into(h);
  if (h.finish() != ref_hash<Col>(m)) {
    fprintf(stderr, "[%s shift %d] content hash diverges at step %" PRId64 "\n", name, shift, step);
    return false;
  }
  io::ByteWriter w;
  c.write(w);
  const std::vector<uint8_t> ref = ref_bytes(m);
  if (w.size() != ref.size() || memcmp(w.data(), ref.data(), ref.size()) != 0) {
    fprintf(stderr, "[%s shift %d] serialized bytes diverge at step %" PRId64 "\n", name, shift, step);
    return false;
  }
  return true;
}

template <typename Col> static void run_model(const char *name, int shift, uint64_t seed, int64_t steps)
{
  using T = typename Col::value_type;
  SplitMix64 r(seed);
  const T def = r.coin() ? T{} : Gen<T>::make(r);
  Col c(def, shift);
  Model<T> m;
  m.def = def;
  const uint32_t page = 1u << shift;
  const uint32_t max_n = std::min<uint32_t>(page * 6 + 3, 6000);
  std::vector<Snapshot<Col>> snaps;
  test::FailureLog log(name);
  const bool full = shift <= 4 || (std::is_same_v<T, uint8_t> && shift <= 8);

  auto idx = [&](uint32_t n) -> uint32_t {
    if (n == 0) {
      return 0;
    }
    /* bias toward page boundaries and the tail */
    switch (r.below(4)) {
      case 0: {
        const uint32_t b = uint32_t(r.below(n / page + 1)) * page;
        const uint32_t v = b + uint32_t(r.range(-1, 1));
        return v < n ? v : n - 1;
      }
      case 1:
        return n - 1 - uint32_t(r.below(std::min<uint32_t>(n, 3)));
      default:
        return uint32_t(r.below(n));
    }
  };

  for (int64_t step = 0; step < steps; step++) {
    log.count_case();
    const uint32_t n = uint32_t(m.v.size());
    switch (r.below(14)) {
      case 0: { /* resize (grow or shrink) */
        const uint32_t nn = uint32_t(r.below(max_n + 1));
        c.resize(nn);
        m.v.resize(nn, m.def);
        break;
      }
      case 1:
      case 2:
        if (n) {
          const uint32_t i = idx(n);
          const T v = Gen<T>::make(r);
          c.set(i, v);
          m.v[i] = v;
        }
        break;
      case 3:
        if (n) {
          const uint32_t i = idx(n);
          const T v = Gen<T>::make(r);
          c.write(i) = v;
          m.v[i] = v;
        }
        break;
      case 4:
        if (n) {
          const uint32_t a = idx(n), b = idx(n);
          const uint32_t lo = std::min(a, b), cnt = std::max(a, b) - lo + 1;
          const T v = r.coin() ? m.def : Gen<T>::make(r);
          c.fill(lo, cnt, v);
          for (uint32_t k = lo; k < lo + cnt; k++) {
            m.v[k] = v;
          }
        }
        break;
      case 5:
        if (n) {
          const uint32_t a = idx(n), b = idx(n);
          const uint32_t lo = std::min(a, b), cnt = std::max(a, b) - lo + 1;
          c.set_default_range(lo, cnt);
          for (uint32_t k = lo; k < lo + cnt; k++) {
            m.v[k] = m.def;
          }
          std::vector<uint32_t> list;
          for (int k = 0; k < 5; k++) {
            list.push_back(idx(n));
          }
          c.set_default_list(util::span<const uint32_t>(list.data(), list.size()));
          for (uint32_t k : list) {
            m.v[k] = m.def;
          }
        }
        break;
      case 6:
        if (n) {
          const uint32_t a = idx(n), b = idx(n);
          c.swap(a, b);
          std::swap(m.v[a], m.v[b]);
        }
        break;
      case 7: { /* permute: random injective map with drops */
        const uint32_t nn = uint32_t(r.below(max_n + 1));
        std::vector<uint32_t> targets(nn);
        for (uint32_t k = 0; k < nn; k++) {
          targets[k] = k;
        }
        for (uint32_t k = nn; k > 1; k--) {
          std::swap(targets[k - 1], targets[r.below(k)]);
        }
        std::vector<uint32_t> map(n, ColumnBase::kDropped);
        for (uint32_t o = 0; o < n && o < nn; o++) {
          if (r.below(5)) {
            map[o] = targets[o];
          }
        }
        c.permute(util::span<const uint32_t>(map.data(), map.size()), nn);
        std::vector<T> nv(nn, m.def);
        for (uint32_t o = 0; o < n; o++) {
          if (map[o] != ColumnBase::kDropped) {
            nv[map[o]] = m.v[o];
          }
        }
        m.v = nv;
        break;
      }
      case 8:
      case 9:
        if (n) { /* mix: random valid batch, simultaneous semantics, aliasing allowed */
          std::vector<uint32_t> targets, offsets{0}, sources;
          std::vector<double> weights;
          std::vector<uint8_t> used(n, 0);
          const int nt = int(r.below(8)) + 1;
          for (int k = 0; k < nt; k++) {
            const uint32_t t = idx(n);
            if (used[t]) {
              continue;
            }
            used[t] = 1;
            targets.push_back(t);
            const int ns = int(r.below(4));
            for (int q = 0; q < ns; q++) {
              sources.push_back(r.coin() ? t : idx(n)); /* targets as sources */
              weights.push_back(r.below(4) == 0 ? 0.0 : r.unit() * 2.0 - 0.5);
            }
            offsets.push_back(uint32_t(sources.size()));
          }
          const bool use_w = r.coin();
          MixPolicy pols[] = {MixPolicy::CopyFirst,
                              MixPolicy::SetDefault,
                              MixPolicy::Interpolate,
                              MixPolicy::WeightedMean,
                              MixPolicy::Sum};
          const MixPolicy p = pols[r.below(5)];
          MixBatch b{util::span<const uint32_t>(targets.data(), targets.size()),
                     util::span<const uint32_t>(offsets.data(), offsets.size()),
                     util::span<const uint32_t>(sources.data(), sources.size()),
                     use_w ? util::span<const double>(weights.data(), weights.size()) :
                             util::span<const double>()};
          ValidatedMixBatch vb;
          const char *err = nullptr;
          if (!log.check(ValidatedMixBatch::validate(b, n, n, vb, &err), [&] { fprintf(stderr, "validate: %s", err); })) {
            break;
          }
          MixContext ctx;
          const bool supported = c.mix(vb, p, ctx, &err);
          const bool expect_supported = Col::ops_type::supports(p);
          if (!log.check(supported == expect_supported,
                         [&] { fprintf(stderr, "policy support mismatch (policy %d)", int(p)); }))
          {
            break;
          }
          if (!supported) {
            break;
          }
          /* reference: gather from the pre-state, then scatter */
          std::vector<T> results;
          for (size_t k = 0; k < targets.size(); k++) {
            const uint32_t s0 = offsets[k], s1 = offsets[k + 1];
            if (s0 == s1 || p == MixPolicy::SetDefault) {
              results.push_back(m.def);
              continue;
            }
            if (p == MixPolicy::CopyFirst) {
              results.push_back(m.v[sources[s0]]);
              continue;
            }
            std::vector<T> sv;
            std::vector<double> wv;
            for (uint32_t q = s0; q < s1; q++) {
              sv.push_back(m.v[sources[q]]);
              if (use_w) {
                wv.push_back(weights[q]);
              }
            }
            results.push_back(ref_combine<T>(p, sv, wv));
          }
          for (size_t k = 0; k < targets.size(); k++) {
            m.v[targets[k]] = results[k];
          }
        }
        break;
      case 10:
        if (n) { /* parallel span writes */
          const uint32_t a = idx(n), b = idx(n);
          const uint32_t lo = std::min(a, b), cnt = std::max(a, b) - lo + 1;
          const T v = Gen<T>::make(r);
          c.parallel_write(lo, cnt, [&](util::span<T> s, uint32_t base) {
            for (size_t k = 0; k < s.size(); k++) {
              s[k] = ((base + k) % 3 == 0) ? v : m.def;
            }
          });
          for (uint32_t k = lo; k < lo + cnt; k++) {
            m.v[k] = (k % 3 == 0) ? v : m.def;
          }
        }
        break;
      case 11: { /* snapshot (at most 8 live, rotating) */
        Snapshot<Col> s{c.share(), m};
        if (snaps.size() >= 8) {
          snaps.erase(snaps.begin() + int(r.below(snaps.size())));
        }
        snaps.push_back(std::move(s));
        break;
      }
      case 12: { /* serialize + read back into a fresh column */
        io::ByteWriter w;
        c.write(w);
        Col d(def, shift);
        io::ByteReader rd(w.data(), w.size());
        log.check(d.read_data(rd, ReadLimits{}) && rd.at_end(), [&] { fprintf(stderr, "read_data failed"); });
        hash::Hasher128 h1(hash::DomainTag::Content), h2(hash::DomainTag::Content);
        c.hash_into(h1);
        d.hash_into(h2);
        log.check(h1.finish() == h2.finish(), [&] { fprintf(stderr, "round trip differs"); });
        break;
      }
      default: { /* shift-independence: same contents at another page shift */
        if (step % 16 == 0) {
          Col other(def, shift == 2 ? 5 : 2);
          other.resize(uint32_t(m.v.size()));
          for (uint32_t k = 0; k < m.v.size(); k++) {
            other.set(k, m.v[k]);
          }
          io::ByteWriter w1, w2;
          c.write(w1);
          other.write(w2);
          log.check(w1.size() == w2.size() && memcmp(w1.data(), w2.data(), w1.size()) == 0,
                    [&] { fprintf(stderr, "bytes depend on page shift"); });
        }
        break;
      }
    }
    if (full || step % 64 == 0) {
      if (!log.check(check(name, c, m, step, shift), [] {})) {
        break;
      }
      for (auto &s : snaps) {
        if (!log.check(check(name, static_cast<const Col &>(*s.col), s.model, step, shift),
                       [] { fprintf(stderr, "a snapshot changed"); }))
        {
          break;
        }
      }
    }
  }
  snaps.clear();
  log.finish();
}

template <typename Col> static void run_all(const char *name, std::initializer_list<int> shifts, uint64_t seed)
{
  const int64_t base = test::cases(20000);
  for (int s : shifts) {
    run_model<Col>(name, s, seed + uint64_t(s), base);
  }
}

int main()
{
  ColumnTypeRegistry::global().add_or_die<Column<F8>>(); /* "<elem>[N]" keys are registered explicitly */
  run_all<Column<float>>("f32", {2, 4, 12}, 1);
  run_all<Column<math::double3>>("f64x3", {2, 4, 12}, 2);
  run_all<Column<int32_t>>("i32", {2, 4, 12}, 3);
  run_all<Column<uint64_t>>("u64", {2, 4, 12}, 4);
  run_all<Column<F8>>("f32[8]", {2, 4}, 5);
  run_all<Column<hash::Hash128>>("h128", {2, 4}, 6);
  run_all<BoolColumn>("bool", {2, 6, 8}, 7);
  return test_end();
}
