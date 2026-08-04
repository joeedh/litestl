#pragma once

#ifndef LITESTL_WORKERS_COUNT
#define LITESTL_WORKERS_COUNT 12
#endif

#include "platform/cpu.h"
#include "util/alloc.h"
#include "util/compiler_util.h"
#include "util/function.h"
#include "util/index_range.h"
#include "util/vector.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <new>
#include <thread>

/** Thread pool and parallel execution utilities. */
namespace litestl::task {
using ThreadMain = std::function<void()>;
using litestl::util::Vector;

namespace detail {

#ifdef __cpp_lib_hardware_interference_size
constexpr size_t kCacheLine = std::hardware_destructive_interference_size;
#else
constexpr size_t kCacheLine = 64;
#endif

/** One iteration of a wait spin: hints the core to back off without yielding
 * the OS timeslice. */
inline void cpu_relax()
{
#if defined(__EMSCRIPTEN__)
  std::this_thread::yield();
#elif defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
  __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__) || defined(_M_ARM64)
  __asm__ __volatile__("yield" ::: "memory");
#else
  std::this_thread::yield();
#endif
}

// Spin budgets before falling back to an OS park. Submissions arrive in tight
// bursts (a sculpt dab runs several parallel_for calls back to back), and a
// park/wake round trip on Windows costs more than a whole band of work.
#if defined(__EMSCRIPTEN__)
constexpr int kIdleSpins = 0;
constexpr int kJoinSpins = 0;
#else
constexpr int kIdleSpins = 4096;
constexpr int kJoinSpins = 4096;
#endif

/**
 * One worker's LIFO task queue plus its OS thread.
 *
 * Queues are per-worker so submission contention stays low, but the pool is
 * work-stealing: an idle worker drains its own queue first, then steals from
 * peers (see TaskPool::try_get_task), so a lopsided distribution never leaves
 * a core idle while work remains anywhere. Idle workers park on the shared
 * pool cv (not a per-worker one), so any submission can wake any sleeper.
 *
 * Cache-line aligned so neighbouring workers' mutexes/queues don't
 * false-share.
 */
struct alignas(kCacheLine) TaskWorker {
  /* LIFO for O(1) pop_back; fairness across submissions isn't a goal. */
  Vector<ThreadMain> queue;
  std::mutex mutex;
  std::thread thread_;

  TaskWorker() = default;

  ~TaskWorker()
  {
    /* Threads are joined by TaskPool's destructor before the pool's
     * coordination state is torn down; this is a no-op safety net. */
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  /** Pops this worker's most-recent task. Returns false if the queue is empty. */
  bool try_pop(ThreadMain &out)
  {
    std::lock_guard guard(mutex);
    if (queue.size() == 0) {
      return false;
    }
    out = queue.pop_back();
    return true;
  }

  /** Appends a task; the caller is responsible for signalling the pool cv. */
  void push_local(ThreadMain main)
  {
    std::lock_guard guard(mutex);
    /* Queue storage is held for the program's lifetime — don't count it
     * as a leak. */
    litestl::alloc::PermanentGuard leakguard;
    queue.append(std::move(main));
  }

  /** Returns the current queue size. */
  int size()
  {
    std::lock_guard guard(mutex);
    return int(queue.size());
  }
};

/**
 * The global work-stealing thread pool.
 *
 * A fixed worker array plus the shared park/wake coordination. `pending`
 * counts tasks queued across all workers; idle workers sleep on `cv` until it
 * goes positive (or stop is signalled). Threads start lazily, all at once, on
 * the first submission — every worker must be live for stealing to balance
 * load.
 *
 * The array is sized to the compile-time max (LITESTL_WORKERS_COUNT), but only
 * `active_` of them are spawned and used, clamped to the machine's core count
 * so we neither oversubscribe small CPUs nor cap large ones below the array.
 */
struct TaskPool {
  TaskWorker workers[LITESTL_WORKERS_COUNT];

  std::mutex mutex; // pairs with cv; publishes `pending` to parked workers
  std::condition_variable cv;
  std::atomic<int> pending{0}; // total queued tasks across all workers
  std::atomic<bool> stop_{false};
  std::atomic<bool> started{false};
  std::atomic<int> next_worker{0};
  const int active_ = clamped_worker_count();

  /** Live worker count: hardware concurrency, clamped to [1, array size]. */
  static int clamped_worker_count()
  {
    const int n = platform::cpu_core_count();
    return n < 1 ? 1 : (n > LITESTL_WORKERS_COUNT ? LITESTL_WORKERS_COUNT : n);
  }

  int worker_count() const
  {
    return active_;
  }

  ~TaskPool()
  {
    {
      std::lock_guard guard(mutex);
      stop_.store(true, std::memory_order_release);
    }
    cv.notify_all();
    /* Join here, while the coordination state (mutex/cv/pending) is still
     * alive — a running worker may touch it until it observes stop. */
    for (int i = 0; i < worker_count(); i++) {
      if (workers[i].thread_.joinable()) {
        workers[i].thread_.join();
      }
    }
  }

  /** Spawns all worker threads on first use (idempotent, thread-safe). */
  void ensure_started()
  {
    if (started.load(std::memory_order_acquire)) {
      return;
    }
    bool expected = false;
    if (!started.compare_exchange_strong(expected, true)) {
      return; // another thread won the race and is spawning them
    }
    for (int i = 0; i < worker_count(); i++) {
      workers[i].thread_ = std::thread([this, i]() { worker_main(i); });
    }
  }

  /**
   * Finds the next task to run for worker @p id: its own queue first, then
   * steals from peers in round-robin order. Returns false if all queues are
   * empty. Decrements `pending` for the task it claims.
   */
  bool try_get_task(int id, ThreadMain &out)
  {
    if (workers[id].try_pop(out)) {
      pending.fetch_sub(1, std::memory_order_acq_rel);
      return true;
    }
    const int n = worker_count();
    for (int i = 1; i < n; i++) {
      if (workers[(id + i) % n].try_pop(out)) {
        pending.fetch_sub(1, std::memory_order_acq_rel);
        return true;
      }
    }
    return false;
  }

  /**
   * Worker loop: run whatever can be found (own queue or a steal), else park
   * on the shared cv until a submission arrives or stop is signalled.
   */
  void worker_main(int id)
  {
    for (;;) {
      ThreadMain task;
      if (try_get_task(id, task)) {
        task();
        continue;
      }
      // Poll `pending` briefly before paying for a park. Stealing is not
      // retried here — a submitter always bumps `pending` after its pushes, so
      // the counter is the cheap proxy for "something is queued somewhere".
      bool retry = false;
      for (int i = 0; i < kIdleSpins; i++) {
        if (pending.load(std::memory_order_acquire) > 0) {
          retry = true;
          break;
        }
        // Leave the spin on stop without retrying: the exit check lives below
        // the wait, so restarting the loop here would busy-spin forever
        // instead of letting the thread finish.
        if (stop_.load(std::memory_order_acquire)) {
          break;
        }
        cpu_relax();
      }
      if (retry) {
        continue;
      }
      std::unique_lock lock(mutex);
      cv.wait(lock, [this] {
        return pending.load(std::memory_order_acquire) > 0 ||
               stop_.load(std::memory_order_acquire);
      });
      if (stop_.load(std::memory_order_acquire) &&
          pending.load(std::memory_order_acquire) == 0) {
        return;
      }
      /* Woke with work (or spuriously) — loop and try_get_task again. */
    }
  }

  /** Queues @p main on @p target without announcing it. Every push must be
   * followed by a matching publish() before the caller waits on the result. */
  void push(int target, ThreadMain main)
  {
    ensure_started();
    /* push_local happens-before the pending increment, so any worker that
     * observes pending>0 is guaranteed to find the task in a queue. */
    workers[target].push_local(std::move(main));
  }

  /** Announces @p count freshly pushed tasks and wakes sleepers. A steal that
   * beats the announcement drives `pending` transiently negative; pushes and
   * pops still balance, and this publish follows immediately, so no task is
   * left queued behind a zero counter. */
  void publish(int count)
  {
    {
      /* Publish `pending` under the pool mutex so a worker between its
       * predicate check and its sleep can't miss this wakeup. */
      std::lock_guard guard(mutex);
      pending.fetch_add(count, std::memory_order_release);
    }
    if (count == 1) {
      cv.notify_one();
    } else {
      cv.notify_all();
    }
  }

  /** Submits @p main to worker @p target and wakes one sleeper. */
  void submit(int target, ThreadMain main)
  {
    push(target, std::move(main));
    publish(1);
  }

  /** Round-robin initial placement; stealing corrects any imbalance. */
  int next_target()
  {
    return next_worker.fetch_add(1, std::memory_order_relaxed) % worker_count();
  }
};

/** The single global pool instance (defined in task.cc). */
extern TaskPool pool;

} // namespace detail

/** Submits @p cb for asynchronous execution on the worker pool. */
template <typename Callback> static void run(Callback cb)
{
  detail::pool.submit(detail::pool.next_target(), std::move(cb));
}

/**
 * Splits @p range into contiguous bands distributed across the worker pool
 * and blocks until all bands complete.
 *
 * Falls back to a single synchronous call when the range size is at or below
 * @p grain_size. Otherwise the range is cut into `min(worker_count,
 * ceil(size/grain_size))` contiguous bands: the calling thread runs the last
 * band itself (no handoff) while the pool runs the rest, and @p cb is invoked
 * exactly once per band. @p grain_size therefore only bounds the band count,
 * not the number of callback calls.
 *
 * Completion is signalled through an RAII guard, so a callback that throws
 * still releases the caller rather than deadlocking it (a throw inside a
 * pooled band is otherwise fatal, as before).
 *
 * A callback must not itself call parallel_for from a pool worker unless the
 * pool has spare workers — nested waits can starve a bounded pool.
 *
 * @p cb signature: `[&](IndexRange range) {}`
 */
template <typename Callback>
void parallel_for(util::IndexRange range, Callback cb, int grain_size = 1)
{
  using namespace util;

  if (range.size <= grain_size) {
    cb(range);
    return;
  }

  const int outer_end = range.start + range.size;
  const int task_count = (range.size + grain_size - 1) / grain_size;

  const int worker_count = detail::pool.worker_count();
  const int submission_count = std::min(worker_count, task_count);

  /* Band s spans grain-chunks [first_task, last_task); as one contiguous
   * IndexRange that is [range.start + grain*first_task, ... last_task), with
   * the final band clamped to outer_end. */
  auto band_start = [&](int s) {
    const int first_task = int((int64_t(task_count) * s) / submission_count);
    return range.start + grain_size * first_task;
  };
  auto band_end = [&](int s) {
    const int last_task = int((int64_t(task_count) * (s + 1)) / submission_count);
    return std::min(range.start + grain_size * last_task, outer_end);
  };

  std::mutex done_mutex;
  std::condition_variable done_cv;
  const int farmed = submission_count - 1; // caller runs the last band itself
  std::atomic<int> remaining{farmed};

  /* Each farmed band is a stack-resident task referenced by a function_ref,
   * which is trivially copyable and small enough to live inline in the queue's
   * std::function — no per-submission heap allocation. The tasks outlive the
   * pooled work because the caller blocks below (Joiner) until it has drained. */
  struct BandTask {
    int b0, b1;
    Callback *cb;
    std::atomic<int> *remaining;
    std::mutex *done_mutex;
    std::condition_variable *done_cv;

    void operator()() const
    {
      /* Signal completion on any exit — including a throwing cb — so the
       * caller's wait can never hang. The decrement must happen UNDER the
       * mutex: the waiter's predicate reads `remaining` while holding it, so
       * decrementing outside lets a spuriously-woken waiter observe 0, return,
       * and unwind the stack this mutex/cv live on before our lock/notify
       * (a real crash — notify_one on a destroyed cv). */
      struct Signal {
        std::atomic<int> &remaining;
        std::mutex &mutex;
        std::condition_variable &cv;
        ~Signal()
        {
          std::lock_guard guard(mutex);
          if (remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            cv.notify_one();
          }
        }
      } signal{*remaining, *done_mutex, *done_cv};

      (*cb)(IndexRange(b0, b1 - b0));
    }
  };

  BandTask tasks[LITESTL_WORKERS_COUNT]{};
  for (int s = 0; s < farmed; s++) {
    tasks[s] = BandTask{
        band_start(s), band_end(s), &cb, &remaining, &done_mutex, &done_cv};
    detail::pool.push(detail::pool.next_target(),
                      util::function_ref<void()>(tasks[s]));
  }
  // One announcement for the whole fan-out: the pool mutex is global, so a
  // lock/notify per band serializes the submissions against each other and
  // against every worker waking up to take one.
  if (farmed > 0) {
    detail::pool.publish(farmed);
  }

  /* Wait for the farmed bands before this frame unwinds, even if the inline
   * band throws — the pooled closures reference our stack. */
  struct Joiner {
    std::atomic<int> &remaining;
    std::mutex &mutex;
    std::condition_variable &cv;
    int farmed;
    ~Joiner()
    {
      if (farmed == 0) {
        return;
      }
      // The caller ran a band of its own, so the others are usually done —
      // spin rather than sleep. Taking `mutex` once on the way out proves no
      // band is still inside notify_one on a cv this frame is about to unwind.
      for (int i = 0; i < detail::kJoinSpins; i++) {
        if (remaining.load(std::memory_order_acquire) == 0) {
          std::lock_guard guard(mutex);
          return;
        }
        detail::cpu_relax();
      }
      std::unique_lock lock(mutex);
      cv.wait(lock, [this] {
        return remaining.load(std::memory_order_acquire) == 0;
      });
    }
  } joiner{remaining, done_mutex, done_cv, farmed};

  const int last_start = band_start(farmed);
  cb(IndexRange(last_start, band_end(farmed) - last_start));
}

} // namespace litestl::task
