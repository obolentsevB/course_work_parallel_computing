#include "llm/ollama_client.h"

#include <httplib.h>

#include <nlohmann/json.hpp>

namespace llm {

using json = nlohmann::json;

OllamaClient::OllamaClient(std::string url, std::string model, size_t max_queue,
                           std::chrono::milliseconds health_ttl)
    : url_(std::move(url)),
      model_(std::move(model)),
      max_queue_(max_queue),
      health_ttl_(health_ttl),
      worker_([this] { WorkerLoop(); }) {}

OllamaClient::~OllamaClient() {
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
  }
  cv_.notify_all();
  worker_.join();
}

std::optional<std::string> OllamaClient::Generate(const std::string& prompt,
                                                  Clock::time_point deadline) {
  auto job = std::make_shared<Job>();
  job->prompt = prompt;
  job->deadline = deadline;
  {
    std::lock_guard lock(mutex_);
    if (queue_.size() >= max_queue_) return std::nullopt;  // back-pressure
    queue_.push_back(job);
  }
  cv_.notify_one();

  std::unique_lock lock(job->mutex);
  const bool done =
      job->cv.wait_until(lock, deadline, [&] { return job->state == JobState::kDone; });
  if (!done) {
    job->state = JobState::kCancelled;  // the worker will skip or discard it
    return std::nullopt;
  }
  return job->result;
}

size_t OllamaClient::queue_length() const {
  std::lock_guard lock(mutex_);
  return queue_.size();
}

void OllamaClient::WorkerLoop() {
  for (;;) {
    std::shared_ptr<Job> job;
    {
      std::unique_lock lock(mutex_);
      cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
      if (stopping_) return;
      job = queue_.front();
      queue_.pop_front();
    }
    {
      std::lock_guard lock(job->mutex);
      if (job->state == JobState::kCancelled) continue;  // caller gave up already
      job->state = JobState::kRunning;
    }
    auto result = Call(job->prompt, job->deadline);  // HTTP call without locks held
    {
      std::lock_guard lock(job->mutex);
      if (job->state == JobState::kCancelled) continue;
      job->result = std::move(result);
      job->state = JobState::kDone;
    }
    job->cv.notify_all();
  }
}

std::optional<std::string> OllamaClient::Call(const std::string& prompt,
                                              Clock::time_point deadline) {
  const auto left = deadline - Clock::now();
  if (left <= Clock::duration::zero()) return std::nullopt;

  httplib::Client client(url_);
  client.set_connection_timeout(std::chrono::seconds(2));
  client.set_read_timeout(std::chrono::duration_cast<std::chrono::milliseconds>(left));

  const json body = {{"model", model_},  {"prompt", prompt},    {"stream", false},
                     {"format", "json"}, {"keep_alive", "30m"}, {"options", {{"temperature", 0}}}};
  auto res = client.Post("/api/generate", body.dump(), "application/json");
  if (!res || res->status != 200) return std::nullopt;

  const json answer = json::parse(res->body, nullptr, /*allow_exceptions=*/false);
  if (!answer.is_object() || !answer.contains("response") || !answer["response"].is_string()) {
    return std::nullopt;
  }
  return answer["response"].get<std::string>();
}

bool OllamaClient::IsAvailable() {
  std::lock_guard lock(health_mutex_);  // at most one probe at a time
  const auto now = Clock::now();
  if (checked_at_ != Clock::time_point{} && now - checked_at_ < health_ttl_) {
    return available_cache_;
  }

  bool available = false;
  httplib::Client client(url_);
  client.set_connection_timeout(std::chrono::seconds(1));
  client.set_read_timeout(std::chrono::seconds(2));
  if (auto res = client.Get("/api/tags"); res && res->status == 200) {
    const json tags = json::parse(res->body, nullptr, false);
    const std::string wanted = model_.find(':') == std::string::npos ? model_ + ":latest" : model_;
    if (tags.is_object() && tags.contains("models")) {
      for (const auto& m : tags["models"]) {
        if (m.value("name", "") == wanted) available = true;
      }
    }
  }

  available_cache_ = available;
  checked_at_ = now;
  if (available && !ever_available_) {
    ever_available_ = true;
    Warmup();
  }
  return available;
}

// Queues an empty prompt: Ollama loads the model into memory, so the first
// real query does not pay the model loading time. Nobody waits for this job.
void OllamaClient::Warmup() {
  auto job = std::make_shared<Job>();
  job->deadline = Clock::now() + std::chrono::minutes(5);
  {
    std::lock_guard lock(mutex_);
    queue_.push_back(job);
  }
  cv_.notify_one();
}

}  // namespace llm