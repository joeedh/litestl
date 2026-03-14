#pragma once

#ifndef LITESTL_WORKERS_COUNT
#define LITESTL_WORKERS_COUNT 6
#endif

#include "platform/cpu.h"
#include "platform/time.h"
#include "util/alloc.h"
#include "util/index_range.h"
#include "util/vector.h"

#include <chrono>
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
 * Auto-starts a detached thread on first push and sleeps with a 2ms
 * timeout when the queue is empty. Call stop() to signal the worker
 * to exit after draining remaining tasks.
 */
struct TaskWorker {
  Vector<ThreadMain> queue;
  std::recursive_mutex mutex;
  std::mutex wait_mutex;
  std::condition_variable cv;
  bool running = false;
  bool stop_ = false;

  TaskWorker()
  {
  }

  /** Signals the worker to stop after draining its queue. */
  void stop()
  {
    stop_ = true;
  }

  /** Starts the worker thread if not already running. */
  void start()
  {
    {
      std::lock_guard guard(mutex);
      if (running) {
        return;
      }
      running = true;
    }

    std::thread *thread = new std::thread([&]() { this->run(); });
    thread->detach();
    delete thread;
  }

  /**
   * Main worker loop.
   *
   * Drains the queue, then sleeps for up to 2ms waiting for new tasks.
   * Exits when stop() has been called and the queue is empty.
   */
  void run()
  {
    using namespace std::chrono_literals;

    while (1) {
      while (!this->empty()) {
        ThreadMain main = this->pop();
        main();
      }

      int size = 0;
      {
        std::lock_guard guard(mutex);
        size = queue.size();
        if (stop_ && !size) {
          printf("worker stopping\n\n");
          break;
        }
      }

      if (size == 0) {
        std::unique_lock lock(wait_mutex);
        const std::chrono::time_point<std::chrono::steady_clock> start =
            std::chrono::steady_clock::now();

        cv.wait_until(lock, start + 2ms);
      }
    }
  }

  /** @deprecated Old recursive drain loop, replaced by run(). */
  void run_old()
  {
    using namespace std::chrono_literals;

    while (!this->empty()) {
      ThreadMain main = this->pop();
      main();
    }

    bool empty;
    {
      std::lock_guard guard(mutex);
      empty = this->empty();
      if (empty) {
        running = false;
      }
    }

    if (!empty) {
      run();
    }
  }

  /** Returns the current queue size. */
  int size()
  {
    return queue.size();
  }

  /**
   * Enqueues a task, waking or starting the worker as needed.
   *
   * If the worker is running and the queue was empty, notifies the
   * condition variable. If the worker is not running, starts it.
   */
  void push(ThreadMain main)
  {
    bool notify = false;

    {
      std::lock_guard guard(mutex);
      // don't store allocated memory in leak list
      litestl::alloc::PermanentGuard leakguard;
      queue.append(main);
      if (queue.size() == 1) {
        notify = true;
      }
    }

    if (running && notify) {
      cv.notify_all();
    }

    {
      std::lock_guard guard(mutex);
      if (!running) {
        start();
      }
    }
  }
  /** Dequeues and returns the last task. */
  ThreadMain pop()
  {
    std::lock_guard guard(mutex);
    ThreadMain ret = queue.pop_back();
    return ret;
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
extern int curWorker;
/** Guards @p curWorker. */
extern std::recursive_mutex curWorkerMutex;

/** Returns the next worker via round-robin selection. */
static TaskWorker &getWorker()
{
#if 0
  int minWorker = 0;
  int minCount = workers[0].size();

  for (int i = 0; i < array_size(workers); i++) {
    if (workers[i].size() < minCount) {
      minWorker = i;
      minCount = workers[i].size();
    }
  }

  printf("using worker %ds\n", minWorker);
  return workers[minWorker];
#else
  std::lock_guard guard(curWorkerMutex);

  int worker = curWorker;
  // printf("using worker %d (with %d outstanding tasks)\n", worker,
  // workers[worker].size());
  curWorker = (curWorker + 1) % array_size(workers);
  return workers[worker];
#endif
}

} // namespace detail

/** Submits @p cb for asynchronous execution on the worker pool. */
template <typename Callback> static void run(Callback cb)
{
#if 0
   auto *thread = new std::thread(cb);
   thread->detach();
   delete thread;
#else
  detail::getWorker().push(cb);
#endif
}

/**
 * Splits @p range into @p grain_size chunks distributed across threads.
 *
 * Spawns up to platform::max_thread_count() threads, distributes chunks
 * round-robin, and blocks until all threads complete. Falls back to a
 * single synchronous call when the range size is at or below @p grain_size.
 *
 * @p cb signature: `[&](IndexRange range) {}`
 */
template <typename Callback>
void parallel_for(util::IndexRange range, Callback cb, int grain_size = 1)
{
  using namespace util;

#if 0
  cb(range);
#else
  if (range.size <= grain_size) {
    cb(range);
    return;
  }

  bool have_remain = false;

  int task_count = range.size / grain_size;
  if (range.size % grain_size) {
    task_count++;
    have_remain = true;
  }

  int thread_count = platform::max_thread_count();

  struct ThreadData {
    Vector<IndexRange> tasks;
    bool done = false;
  };

  Vector<ThreadData> thread_datas;
  thread_datas.resize(thread_count);
  int thread_i = 0;

  for (int i = 0; i < task_count; i++) {
    int start = range.start + grain_size * i;
    IndexRange range;

    if (i == task_count - 1 && have_remain) {
      range = IndexRange(start, range.size - start);
    } else {
      range = IndexRange(start, grain_size);
    }

    thread_datas[thread_i].tasks.append(range);
    thread_i = (thread_i + 1) % thread_count;
  }

  Vector<std::thread *> threads;

  for (int i : IndexRange(thread_count)) {
    std::thread *thread =
        alloc::New<std::thread>("std::thread", std::thread([i, &thread_datas, &cb]() {
                                  for (IndexRange &range : thread_datas[i].tasks) {
                                    cb(range);
                                  }

                                  thread_datas[i].done = true;
                                }));

    threads.append(thread);
  }

  while (1) {
    bool ok = true;
    for (ThreadData &data : thread_datas) {
      ok = ok && data.done;
    }

    if (ok) {
      break;
    }

    /* TODO: use a condition variable. */
    time::sleep_ns(100);
  }

  for (std::thread *thread : threads) {
    thread->join();
    alloc::Delete<std::thread>(thread);
  }
#endif
}
} // namespace litestl::task
