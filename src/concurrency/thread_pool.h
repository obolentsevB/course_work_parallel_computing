#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace concurrency {

// Fixed-size pool of worker threads sharing one FIFO task queue.
// The queue is guarded by a mutex; idle workers sleep on a condition variable.
class ThreadPool {
 public:
  explicit ThreadPool(size_t workers);
  ~ThreadPool();

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  // Returns false if the pool is already shutting down.
  bool Submit(std::function<void()> task);

  // Stops accepting tasks, lets workers drain the queue and joins them.
  void Shutdown();

  size_t worker_count() const { return workers_.size(); }
  size_t queue_size() const;

 private:
  void WorkerLoop();

  std::vector<std::thread> workers_;
  std::deque<std::function<void()>> tasks_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  bool stopping_ = false;
};

}  // namespace concurrency