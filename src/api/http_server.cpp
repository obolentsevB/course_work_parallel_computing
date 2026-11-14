#include "api/http_server.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>

#include "api/json_dto.h"
#include "concurrency/thread_pool.h"
#include "text/tokenizer.h"

namespace api {

namespace {

using Clock = std::chrono::steady_clock;
using dto::Json;

int64_t MillisSince(Clock::time_point start) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
}

void SendJson(httplib::Response& res, const Json& body, int status = 200) {
  res.status = status;
  res.set_content(body.dump(), "application/json");
}

std::vector<std::string> QueryTerms(const std::string& query) {
  std::vector<std::string> terms;
  text::Tokenizer::Tokenize(query, [&](std::string_view t, size_t) {
    if (std::find(terms.begin(), terms.end(), t) == terms.end()) terms.emplace_back(t);
  });
  return terms;
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

// Runs BM25 over a snapshot and attaches snippets. Shared by REST and WS.
Json SearchResults(const Services& s, const indexing::InvertedIndex& index,
                   const std::vector<search::WeightedTerm>& weighted) {
  std::vector<std::string> plain;
  for (const auto& w : weighted) plain.push_back(w.term);

  Json results = Json::array();
  for (const auto& hit : s.searcher.Search(index, weighted, s.config.top_k)) {
    const auto& path = index.doc(hit.doc).rel_path;
    const double score = std::round(hit.score * 1e4) / 1e4;
    results.push_back(dto::Result(path, score, s.snippets.Extract(path, plain)));
  }
  return results;
}

class RestHandlers {
 public:
  explicit RestHandlers(Services& s) : s_(s) {}

  void Health(const httplib::Request&, httplib::Response& res) {
    SendJson(res, dto::Health(s_.ollama.IsAvailable()));
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

  void Search(const httplib::Request& req, httplib::Response& res) {
    const auto start = Clock::now();
    const Json body = Json::parse(req.body, nullptr, false);
    if (!body.is_object() || !body.contains("query") || !body["query"].is_string()) {
      return SendJson(res, dto::Error("field 'query' (string) is required"), 400);
    }
    const std::string mode = body.value("mode", "keyword");
    if (mode != "keyword") return SendJson(res, dto::Error("unsupported mode: " + mode), 400);

    Json results = Json::array();
    if (auto index = s_.holder.Get()) {
      std::vector<search::WeightedTerm> terms;
      for (auto& t : QueryTerms(body["query"])) terms.push_back({t, 1.0});
      results = SearchResults(s_, *index, terms);
    }
    SendJson(res, dto::Search(std::move(results), MillisSince(start)));
  }

  void Stats(const httplib::Request&, httplib::Response& res) {
    auto index = s_.holder.Get();
    SendJson(res, dto::Stats(index ? index->stats() : indexing::IndexStats{}));
  }

 private:
  Services& s_;
};

// One WebSocket connection: each received {"query": ...} runs the pipeline
// query_expansion -> keyword_search -> done, streaming one message per stage.
class WsSearchHandler {
 public:
  explicit WsSearchHandler(Services& s) : s_(s) {}

  void Handle(const httplib::Request&, httplib::ws::WebSocket& ws) {
    std::string message;
    while (ws.read(message) == httplib::ws::ReadResult::Text) {
      const Json request = Json::parse(message, nullptr, false);
      if (!request.is_object() || !request.contains("query") || !request["query"].is_string()) {
        ws.send(dto::Stage("error", {{"message", "field 'query' (string) is required"}}).dump());
        continue;
      }
      const std::string query = request["query"];

      // Stage 1: LLM query expansion (empty list if Ollama is unavailable).
      const auto deadline = Clock::now() + std::chrono::milliseconds(s_.config.ollama_timeout_ms);
      const auto expanded = s_.expander.Expand(query, deadline);
      ws.send(dto::Stage("query_expansion", {{"original", query}, {"expanded_terms", expanded}})
                  .dump());

      // Stage 2: BM25 search, LLM terms get a lower weight than the user's terms.
      const auto start = Clock::now();
      Json results = Json::array();
      if (auto index = s_.holder.Get()) {
        std::vector<search::WeightedTerm> terms;
        for (auto& t : QueryTerms(query)) terms.push_back({t, 1.0});
        for (auto& t : expanded) terms.push_back({t, s_.config.expansion_weight});
        results = SearchResults(s_, *index, terms);
      }
      ws.send(dto::Stage("keyword_search", {{"results", results}, {"time_ms", MillisSince(start)}})
                  .dump());

      ws.send(dto::Stage("done", nullptr).dump());
    }
  }

 private:
  Services& s_;
};

}  // namespace

Services::Services(const app::Config& c)
    : config(c),
      builder(c.data_dir, c.index_segments, c.index_batch_docs),
      snippets(c.data_dir, c.snippet_radius),
      ollama(c.ollama_url, c.ollama_model, c.ollama_max_queue,
             std::chrono::milliseconds(c.ollama_health_ttl_ms)),
      expander(ollama, c.max_expanded_terms) {}

HttpServer::HttpServer(const app::Config& config) : services_(std::make_unique<Services>(config)) {
  const size_t threads = config.http_threads;
  server_.new_task_queue = [threads] { return new PoolTaskQueue(threads); };
  RegisterRoutes();
}

void HttpServer::RegisterRoutes() {
  // Handlers are stateless wrappers over Services, shared by all threads.
  auto rest = std::make_shared<RestHandlers>(*services_);
  auto ws = std::make_shared<WsSearchHandler>(*services_);

  server_.Get("/api/health", [rest](auto& req, auto& res) { rest->Health(req, res); });
  server_.Post("/api/index", [rest](auto& req, auto& res) { rest->Index(req, res); });
  server_.Post("/api/search", [rest](auto& req, auto& res) { rest->Search(req, res); });
  server_.Get("/api/stats", [rest](auto& req, auto& res) { rest->Stats(req, res); });
  server_.WebSocket("/ws/search", [ws](auto& req, auto& socket) { ws->Handle(req, socket); });
}

bool HttpServer::Run() {
  const auto& c = services_->config;
  std::cout << "listening on :" << c.port << " (http threads: " << c.http_threads
            << ", data: " << c.data_dir << ", ollama: " << c.ollama_url << " " << c.ollama_model
            << ")" << std::endl;
  return server_.listen("0.0.0.0", c.port);
}

}  // namespace api