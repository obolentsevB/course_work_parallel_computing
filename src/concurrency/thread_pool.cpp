#include "concurrency/thread_pool.h"

namespace concurrency {

ThreadPool::ThreadPool(size_t workers) {
  if (workers == 0) workers = 1;
  workers_.reserve(workers);
  for (size_t i = 0; i < workers; ++i) {
    workers_.emplace_back([this] { WorkerLoop(); });
  }
}

ThreadPool::~ThreadPool() { Shutdown(); }

bool ThreadPool::Submit(std::function<void()> task) {
  {
    std::lock_guard lock(mutex_);
    if (stopping_) return false;
    tasks_.push_back(std::move(task));
  }
  cv_.notify_one();  // wake exactly one idle worker
  return true;
}

void ThreadPool::Shutdown() {
  {
    std::lock_guard lock(mutex_);
    if (stopping_) return;
    stopping_ = true;
  }
  cv_.notify_all();
  for (auto& worker : workers_) {
    if (worker.joinable()) worker.join();
  }
}

size_t ThreadPool::queue_size() const {
  std::lock_guard lock(mutex_);
  return tasks_.size();
}

void ThreadPool::WorkerLoop() {
  for (;;) {
    std::function<void()> task;
    {
      std::unique_lock lock(mutex_);
      // The predicate protects against spurious wakeups.
      cv_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
      if (tasks_.empty()) return;  // stopping and nothing left to do
      task = std::move(tasks_.front());
      tasks_.pop_front();
    }
    task();  // executed without holding the lock
  }
}

}  // namespace concurrency