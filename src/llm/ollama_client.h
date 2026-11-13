#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace llm {

using Clock = std::chrono::steady_clock;

enum class JobState { kPending, kRunning, kDone, kCancelled };

// One generation request. The requesting thread waits on `cv`;
// the Ollama thread fills `result` and sets the state to kDone.
struct Job {
  std::string prompt;
  Clock::time_point deadline;
  JobState state = JobState::kPending;
  std::optional<std::string> result;
  std::mutex mutex;
  std::condition_variable cv;
};

// Client for a local Ollama server.
//
// Ollama (with OLLAMA_NUM_PARALLEL=1) processes one generation at a time, so
// requests from many HTTP threads go through a bounded FIFO queue served by a
// single dedicated thread (producer-consumer). Callers wait with a deadline and
// get nullopt on timeout / overflow, so search never blocks forever on the LLM.
class OllamaClient {
 public:
  OllamaClient(std::string url, std::string model, size_t max_queue,
               std::chrono::milliseconds health_ttl);
  ~OllamaClient();

  // Queues `prompt` for /api/generate (JSON output mode) and waits for the
  // answer until `deadline`. Returns the model's "response" text.
  std::optional<std::string> Generate(const std::string& prompt, Clock::time_point deadline);

  // True when Ollama answers and the model is pulled. Cached for health_ttl.
  bool IsAvailable();

  size_t queue_length() const;

 private:
  void WorkerLoop();
  std::optional<std::string> Call(const std::string& prompt, Clock::time_point deadline);
  void Warmup();

  std::string url_;
  std::string model_;
  size_t max_queue_;
  std::chrono::milliseconds health_ttl_;

  std::deque<std::shared_ptr<Job>> queue_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  bool stopping_ = false;
  std::thread worker_;

  std::mutex health_mutex_;
  bool available_cache_ = false;
  bool ever_available_ = false;
  Clock::time_point checked_at_{};
};

}  // namespace llm