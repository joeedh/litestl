#pragma once

#ifndef LITESTL_WORKERS_COUNT
#define LITESTL_WORKERS_COUNT 12
#endif

#include "platform/cpu.h"
#include "util/alloc.h"
#include "util/compiler_util.h"
#include "util/index_range.h"
#include "util/vector.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

/** Thread pool and parallel execution utilities. */
namespace litestl::task {
using ThreadMain = std::function<void()>;
using litestl::util::Vector;

namespace detail {
/**
 * Worker thread that maintains a task queue.
 *
 * Auto-starts a joinable thread on first push. The worker sleeps on a
 * condition variable while idle; push() and stop() both wake it. The
 * destructor signals stop and joins, so workers are always cleanly
 * torn down (assuming queued tasks don't reference state that's
 * already been destroyed).
 */
struct TaskWorker {
  /* LIFO for O(1) pop_back; fairness across submissions isn't a goal. */
  Vector<ThreadMain> queue;
  std::mutex mutex;
  std::condition_variable cv;
  std::atomic<bool> running{false};
  std::atomic<bool> stop_{false};
  std::thread thread_;

  TaskWorker() = default;

  ~TaskWorker()
  {
    {
      std::lock_guard guard(mutex);
      stop_.store(true, std::memory_order_release);
    }
    cv.notify_all();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  /** Signals the worker to stop after draining its queue. */
  void stop()
  {
    {
      std::lock_guard guard(mutex);
      stop_.store(true, std::memory_order_release);
    }
    cv.notify_all();
  }

  /**
   * Main worker loop.
   *
   * Sleeps on the cv until a task is pushed or stop is signaled.
   * Exits when stop is set and the queue is drained.
   */
  void run()
  {
    for (;;) {
      ThreadMain task;
      {
        std::unique_lock lock(mutex);
        cv.wait(lock, [this] {
          return queue.size() != 0 || stop_.load(std::memory_order_acquire);
        });

        if (queue.size() == 0) {
          /* stop_ must be set (predicate). */
          running.store(false, std::memory_order_release);
          return;
        }
        task = queue.pop_back();
      }
      task();
    }
  }

  /** Returns the current queue size. */
  int size()
  {
    std::lock_guard guard(mutex);
    return queue.size();
  }

  /**
   * Enqueues a task, starting or waking the worker as needed.
   *
   * Notify happens under the queue mutex to avoid the classic
   * lost-wakeup race against run()'s cv.wait.
   */
  void push(ThreadMain main)
  {
    {
      std::lock_guard guard(mutex);
      /* Queue storage is held for the program's lifetime — don't count
       * it as a leak. */
      litestl::alloc::PermanentGuard leakguard;
      queue.append(std::move(main));
      cv.notify_one();
    }

    /* Lazy first-time start. CAS makes it safe under concurrent pushes
     * from multiple threads. */
    if (!running.load(std::memory_order_acquire)) {
      bool expected = false;
      if (running.compare_exchange_strong(expected, true)) {
        thread_ = std::thread([this]() { run(); });
      }
    }
  }

  /** Returns true if the queue is empty. */
  bool empty()
  {
    std::lock_guard guard(mutex);
    return queue.size() == 0;
  }
};

/** Global worker pool array sized by LITESTL_WORKERS_COUNT. */
extern TaskWorker workers[LITESTL_WORKERS_COUNT];
/** Round-robin index into the worker pool. */
extern std::atomic<int> curWorker;

/** Returns the next worker via round-robin selection. */
inline TaskWorker &getWorker()
{
  int worker = curWorker.fetch_add(1, std::memory_order_relaxed) %
               int(array_size(workers));
  return workers[worker];
}

} // namespace detail

/** Submits @p cb for asynchronous execution on the worker pool. */
template <typename Callback> static void run(Callback cb)
{
  detail::getWorker().push(std::move(cb));
}

/**
 * Splits @p range into @p grain_size chunks distributed across the
 * worker pool, and blocks until all chunks complete.
 *
 * Falls back to a single synchronous call when the range size is at or
 * below @p grain_size. The number of submissions is capped at both the
 * worker pool size and the chunk count, so small workloads don't pay
 * for empty submissions.
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

  int worker_count = int(array_size(detail::workers));
  const int submission_count = std::min(worker_count, task_count);

  std::mutex done_mutex;
  std::condition_variable done_cv;
  std::atomic<int> remaining{submission_count};

  /* Spread `task_count` chunks evenly across `submission_count`
   * submissions: submission s_idx handles tasks [first, last). */
  for (int s_idx = 0; s_idx < submission_count; s_idx++) {
    const int first_task =
        int((int64_t(task_count) * s_idx) / submission_count);
    const int last_task =
        int((int64_t(task_count) * (s_idx + 1)) / submission_count);

    run([first_task,
         last_task,
         task_count,
         outer_end,
         grain_size,
         range_start = range.start,
         &cb,
         &done_mutex,
         &done_cv,
         &remaining]() {
      for (int t = first_task; t < last_task; t++) {
        const int chunk_start = range_start + grain_size * t;
        const int chunk_size = (t == task_count - 1) ? (outer_end - chunk_start)
                                                     : grain_size;
        cb(IndexRange(chunk_start, chunk_size));
      }

      /* Last-out signals completion. Take the mutex so we never notify
       * between the waiter's predicate check and its sleep. */
      if (remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        std::lock_guard guard(done_mutex);
        done_cv.notify_one();
      }
    });
  }

  std::unique_lock lock(done_mutex);
  done_cv.wait(lock,
               [&] { return remaining.load(std::memory_order_acquire) == 0; });
}

} // namespace litestl::task
