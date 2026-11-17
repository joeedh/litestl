#pragma once

/* Shared helpers for randomized / bulk tests.
 *
 *  - test::scale() / test::cases(n): randomized case counts scale with the
 *    LITESTL_TEST_SCALE environment variable (or the CMake cache default
 *    baked in as LITESTL_TEST_SCALE_DEFAULT).
 *  - test::FailureLog: counts failures in a bulk loop, prints the first few
 *    with caller-supplied detail, then asserts once at the end. Avoids the
 *    trap-on-first-failure behaviour of test_assert inside 10^6-case loops.
 */

#include "test_util.h"

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#ifndef LITESTL_TEST_SCALE_DEFAULT
#define LITESTL_TEST_SCALE_DEFAULT 1
#endif

namespace litestl::test {

inline int64_t scale()
{
  static const int64_t s = [] {
    if (const char *env = getenv("LITESTL_TEST_SCALE")) {
      long long v = atoll(env);
      return int64_t(v > 0 ? v : 1);
    }
    return int64_t(LITESTL_TEST_SCALE_DEFAULT > 0 ? LITESTL_TEST_SCALE_DEFAULT : 1);
  }();
  return s;
}

inline int64_t cases(int64_t base)
{
  return base * scale();
}

class FailureLog {
public:
  explicit FailureLog(const char *name, int max_print = 10)
      : name_(name), max_print_(max_print)
  {
  }

  /* Records a failure; `detail` is invoked (to print inputs) for the first
   * max_print failures only. */
  template <typename Detail> void fail(Detail &&detail)
  {
    if (failures_ < max_print_) {
      fprintf(stderr, "[%s] failure #%" PRId64 ": ", name_, failures_ + 1);
      detail();
      fputc('\n', stderr);
    }
    failures_++;
  }

  template <typename Detail> bool check(bool ok, Detail &&detail)
  {
    if (!ok) {
      fail(detail);
    }
    return ok;
  }

  void count_case()
  {
    cases_++;
  }

  int64_t failures() const
  {
    return failures_;
  }

  /* Prints a summary and asserts (once) that nothing failed. */
  bool finish()
  {
    if (failures_) {
      fprintf(stderr,
              "[%s] %" PRId64 " failure(s) out of %" PRId64 " case(s)\n",
              name_,
              failures_,
              cases_);
    }
    else {
      printf("[%s] ok (%" PRId64 " cases)\n", name_, cases_);
    }
    bool ok = failures_ == 0;
    test_assert(ok);
    return ok;
  }

private:
  const char *name_;
  int max_print_;
  int64_t failures_ = 0;
  int64_t cases_ = 0;
};

} // namespace litestl::test
