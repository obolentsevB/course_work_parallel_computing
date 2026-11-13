#pragma once

#include <httplib.h>

#include <memory>

#include "app/config.h"
#include "index/index_builder.h"
#include "index/inverted_index.h"
#include "llm/ollama_client.h"
#include "search/searcher.h"
#include "search/snippet_extractor.h"

namespace api {

// Everything request handlers share. All members are thread-safe or immutable.
struct Services {
  explicit Services(const app::Config& config);

  app::Config config;
  indexing::IndexHolder holder;
  indexing::IndexBuilder builder;
  search::Searcher searcher;
  search::SnippetExtractor snippets;
  llm::OllamaClient ollama;
  std::mutex build_mutex;  // index builds run one at a time
};

// REST server. Connections are served by our own ThreadPool
// (plugged into cpp-httplib through PoolTaskQueue).
class HttpServer {
 public:
  explicit HttpServer(const app::Config& config);
  bool Run();
  void Stop() { server_.stop(); }

 private:
  void RegisterRoutes();

  std::unique_ptr<Services> services_;
  httplib::Server server_;
};

}  // namespace api