#include "task.h"

namespace litestl::task::detail {
TaskWorker workers[LITESTL_WORKERS_COUNT];
std::atomic<int> curWorker{0};
} // namespace litestl::task::detail
