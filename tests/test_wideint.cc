#include "../math/wideint.h"

#include "bigint_oracle.h"
#include "splitmix64.h"
#include "test_harness.h"

#include <cinttypes>

test_init;

using namespace litestl;
using math::Int128;
using math::Int256;
using math::WideInt;
using test::BigInt;
using test::FailureLog;
using test::SplitMix64;

template <int N> static BigInt big(const WideInt<N> &x)
{
  return BigInt::from_twos(x.w, N);
}

template <int N> static bool matches_mod(const WideInt<N> &x, const BigInt &expected)
{
  auto t = expected.to_twos(N);
  for (int i = 0; i < N; i++) {
    if (t[i] != x.w[i]) {
      return false;
    }
  }
  return true;
}

template <int N> static void print(const char *label, const WideInt<N> &x)
{
  fprintf(stderr, "%s=", label);
  for (int i = N - 1; i >= 0; i--) {
    fprintf(stderr, "%016" PRIx64, x.w[i]);
  }
  fprintf(stderr, " ");
}

/* Mix of uniformly random words and boundary patterns (0, ±1, sign bits,
 * all-ones runs) that exercise carry/borrow chains. */
template <int N> static WideInt<N> gen(SplitMix64 &rng)
{
  static const uint64_t specials[] = {0,
                                      1,
                                      ~uint64_t(0),
                                      uint64_t(1) << 63,
                                      (uint64_t(1) << 63) - 1,
                                      uint64_t(1) << 32,
                                      0xffffffffULL,
                                      0xffffffff00000000ULL};
  WideInt<N> r;
  switch (rng.below(4)) {
    case 0: /* fully random */
      for (int i = 0; i < N; i++) {
        r.w[i] = rng.next();
      }
      break;
    case 1: /* words from special patterns */
      for (int i = 0; i < N; i++) {
        r.w[i] = specials[rng.below(8)];
      }
      break;
    case 2: /* small signed value */
      r = WideInt<N>::from(int64_t(rng.range(-1000, 1000)));
      break;
    default: /* random magnitude: sign-extended random low words */
    {
      int k = int(rng.below(N)) + 1;
      for (int i = 0; i < k; i++) {
        r.w[i] = rng.next();
      }
      const uint64_t ext = (r.w[k - 1] >> 63) ? ~uint64_t(0) : 0;
      for (int i = k; i < N; i++) {
        r.w[i] = ext;
      }
      break;
    }
  }
  return r;
}

static int64_t gen64(SplitMix64 &rng)
{
  static const int64_t specials[] = {0, 1, -1, INT64_MIN, INT64_MAX, INT64_MIN + 1, 1 << 30};
  if (rng.below(4) == 0) {
    return specials[rng.below(7)];
  }
  return int64_t(rng.next());
}

template <int N> static void test_ops(uint64_t seed)
{
  SplitMix64 rng(seed);
  char name[64];
  snprintf(name, sizeof(name), "wideint<%d> ops", N);
  FailureLog log(name);
  const int64_t n = test::cases(200000);

  for (int64_t c = 0; c < n; c++) {
    const WideInt<N> a = gen<N>(rng), b = gen<N>(rng);
    const BigInt A = big(a), B = big(b);
    auto detail = [&] {
      print("a", a);
      print("b", b);
    };
    log.count_case();

    log.check(matches_mod(a + b, A + B), [&] { fprintf(stderr, "add "), detail(); });
    log.check(matches_mod(a - b, A - B), [&] { fprintf(stderr, "sub "), detail(); });
    log.check(matches_mod(-a, -A), [&] { fprintf(stderr, "neg "), detail(); });
    log.check(matches_mod(a * b, A * B), [&] { fprintf(stderr, "mul "), detail(); });

    const int cmp = (a <=> b) < 0 ? -1 : ((a <=> b) > 0 ? 1 : 0);
    log.check(cmp == A.cmp(B), [&] { fprintf(stderr, "cmp "), detail(); });
    log.check((a == b) == (A == B), [&] { fprintf(stderr, "eq "), detail(); });
    log.check(a.sign() == A.sign(), [&] { fprintf(stderr, "sign "), detail(); });

    const int s = int(rng.below(64 * N));
    log.check(matches_mod(a << s, A.shl(s)), [&] { fprintf(stderr, "shl %d ", s), detail(); });
    /* arithmetic shift right == floor(a / 2^s): r*2^s <= a < (r+1)*2^s */
    {
      const BigInt R = big(a >> s).shl(s);
      const BigInt next = R + BigInt::from_i64(1).shl(s);
      log.check(R.cmp(A) <= 0 && A.cmp(next) < 0,
                [&] { fprintf(stderr, "shr %d ", s), detail(); });
    }

    const WideInt<2 * N> p = math::mul_wide(a, b);
    log.check(big(p) == A * B, [&] { fprintf(stderr, "mul_wide "), detail(); });

    const int64_t small = gen64(rng);
    log.check(big(WideInt<N>::from(small)) == BigInt::from_i64(small),
              [&] { fprintf(stderr, "from(%" PRId64 ")", small); });
  }
  log.finish();
}

static void test_mul64(uint64_t seed)
{
  SplitMix64 rng(seed);
  FailureLog log("mul_wide(int64) + umul64 paths");
  const int64_t n = test::cases(500000);
  for (int64_t c = 0; c < n; c++) {
    const int64_t a = gen64(rng), b = gen64(rng);
    log.count_case();
    const Int128 p = math::mul_wide(a, b);
    log.check(big(p) == BigInt::from_i64(a) * BigInt::from_i64(b),
              [&] { fprintf(stderr, "a=%" PRId64 " b=%" PRId64, a, b); });

    uint64_t hp, hd;
    const uint64_t lp = math::detail::portable::umul64(uint64_t(a), uint64_t(b), &hp);
    const uint64_t ld = math::detail::umul64(uint64_t(a), uint64_t(b), &hd);
    const BigInt U = BigInt::from_u64(uint64_t(a)) * BigInt::from_u64(uint64_t(b));
    const uint64_t words[2] = {lp, hp};
    const auto ut = U.to_twos(3);
    log.check(words[0] == ut[0] && words[1] == ut[1],
              [&] { fprintf(stderr, "portable umul64 a=%" PRIx64 " b=%" PRIx64, a, b); });
    log.check(lp == ld && hp == hd,
              [&] { fprintf(stderr, "portable != dispatched umul64 a=%" PRIx64, a); });
  }
  log.finish();
}

/* constexpr usability */
static_assert(math::mul_wide(int64_t(-3), int64_t(7)).sign() == -1);
static_assert((Int128::from(5) - Int128::from(7)) == Int128::from(-2));
static_assert((Int256::from(Int128::from(-1)) << 200).is_negative());
static_assert(sizeof(Int128) == 16 && sizeof(Int256) == 32);
static_assert((Int128::from(INT64_MIN) * Int128::from(-1)).w[0] == uint64_t(1) << 63);

int main()
{
  printf("LITESTL_HAVE_INT128=%d scale=%" PRId64 "\n", LITESTL_HAVE_INT128, test::scale());
  test_mul64(1);
  test_ops<2>(2);
  test_ops<4>(3);
  return test_end();
}
