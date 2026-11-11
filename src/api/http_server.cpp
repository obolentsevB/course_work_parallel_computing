#include "api/http_server.h"

#include <chrono>
#include <iostream>

#include "api/json_dto.h"
#include "concurrency/thread_pool.h"

namespace api {

namespace {

using dto::Json;

void SendJson(httplib::Response& res, const Json& body, int status = 200) {
  res.status = status;
  res.set_content(body.dump(), "application/json");
}

// cpp-httplib asks a TaskQueue to run each accepted connection.
// We hand connections to our own ThreadPool, so N connections are served by
// N worker threads in parallel.
class PoolTaskQueue : public httplib::TaskQueue {
 public:
  explicit PoolTaskQueue(size_t threads) : pool_(threads) {}
  bool enqueue(std::function<void()> fn) override { return pool_.Submit(std::move(fn)); }
  void shutdown() override { pool_.Shutdown(); }

 private:
  concurrency::ThreadPool pool_;
};

class RestHandlers {
 public:
  explicit RestHandlers(Services& s) : s_(s) {}

  void Health(const httplib::Request&, httplib::Response& res) {
    SendJson(res, dto::Health(false));
  }

  void Index(const httplib::Request& req, httplib::Response& res) {
    size_t threads = s_.config.default_threads;
    if (!req.body.empty()) {
      const Json body = Json::parse(req.body, nullptr, false);
      if (!body.is_object()) return SendJson(res, dto::Error("body must be a JSON object"), 400);
      if (body.contains("threads")) {
        const auto& t = body["threads"];
        if (!t.is_number_integer() || t.get<int64_t>() < 1 || t.get<int64_t>() > 256) {
          return SendJson(res, dto::Error("threads must be an integer in [1, 256]"), 400);
        }
        threads = t.get<size_t>();
      }
    }

    std::lock_guard lock(s_.build_mutex);
    std::shared_ptr<const indexing::InvertedIndex> index = s_.builder.Build(threads);
    s_.holder.Publish(index);
    const auto& st = index->stats();
    std::cout << "index built: " << st.documents << " docs, " << index->term_count() << " terms, "
              << st.build_time_ms << " ms, " << st.threads_used << " threads" << std::endl;
    SendJson(res, dto::Index(st.documents, st.build_time_ms));
  }

  void Stats(const httplib::Request&, httplib::Response& res) {
    auto index = s_.holder.Get();
    SendJson(res, dto::Stats(index ? index->stats() : indexing::IndexStats{}));
  }

 private:
  Services& s_;
};

}  // namespace

Services::Services(const app::Config& c)
    : config(c), builder(c.data_dir, c.index_segments, c.index_batch_docs) {}

HttpServer::HttpServer(const app::Config& config) : services_(std::make_unique<Services>(config)) {
  const size_t threads = config.http_threads;
  server_.new_task_queue = [threads] { return new PoolTaskQueue(threads); };
  RegisterRoutes();
}

void HttpServer::RegisterRoutes() {
  // Handlers are stateless wrappers over Services, shared by all threads.
  auto rest = std::make_shared<RestHandlers>(*services_);

  server_.Get("/api/health", [rest](auto& req, auto& res) { rest->Health(req, res); });
  server_.Post("/api/index", [rest](auto& req, auto& res) { rest->Index(req, res); });
  server_.Get("/api/stats", [rest](auto& req, auto& res) { rest->Stats(req, res); });
}

bool HttpServer::Run() {
  const auto& c = services_->config;
  std::cout << "listening on :" << c.port << " (http threads: " << c.http_threads
            << ", data: " << c.data_dir << ", ollama: " << c.ollama_url << " " << c.ollama_model
            << ")" << std::endl;
  return server_.listen("0.0.0.0", c.port);
}

}  // namespace api