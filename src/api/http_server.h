#pragma once

#include <httplib.h>

#include <memory>
#include <thread>

#include "app/config.h"
#include "index/index_builder.h"
#include "index/inverted_index.h"
#include "llm/ollama_client.h"
#include "search/query_expander.h"
#include "search/searcher.h"
#include "search/snippet_extractor.h"

namespace api {

// Everything request handlers share. All members are thread-safe or immutable.
struct Services {
  explicit Services(const app::Config& config);

  // Builds the index and publishes it; builds run one at a time.
  std::shared_ptr<const indexing::InvertedIndex> BuildIndex(size_t threads);

  app::Config config;
  indexing::IndexHolder holder;
  indexing::IndexBuilder builder;
  search::Searcher searcher;
  search::SnippetExtractor snippets;
  llm::OllamaClient ollama;
  search::QueryExpander expander;
  std::mutex build_mutex;  // index builds run one at a time
};

// REST + WebSocket server. Connections are served by our own ThreadPool
// (plugged into cpp-httplib through PoolTaskQueue).
class HttpServer {
 public:
  explicit HttpServer(const app::Config& config);
  ~HttpServer();
  bool Run();
  void Stop() { server_.stop(); }

 private:
  void RegisterRoutes();

  std::unique_ptr<Services> services_;
  httplib::Server server_;
  std::thread startup_index_;  // INDEX_ON_START build
};

}  // namespace api