#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>

namespace concurrency {

// Waits until a counter of outstanding tasks reaches zero (like Go's WaitGroup).
class WaitGroup {
 public:
  void Add(size_t n) {
    std::lock_guard lock(mutex_);
    count_ += n;
  }

  void Done() {
    std::lock_guard lock(mutex_);
    if (--count_ == 0) cv_.notify_all();
  }

  void Wait() {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [this] { return count_ == 0; });
  }

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  size_t count_ = 0;
};

}  // namespace concurrency