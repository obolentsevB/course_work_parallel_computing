#pragma once

#include <cstdlib>
#include <string>
#include <thread>
#include <type_traits>

namespace app {

// Runtime settings, read from environment variables (see docker-compose.yml).
struct Config {
  std::string data_dir = "/data";
  std::string web_dir = "/app/web";
  int port = 8080;
  size_t http_threads = 32;  // request pool size = max concurrent connections
  size_t default_threads = std::thread::hardware_concurrency();
  size_t index_segments = 1024;  // lock stripes of the concurrent hash map
  size_t index_batch_docs = 64;  // documents per thread-pool task
  size_t top_k = 10;
  size_t snippet_radius = 120;
  std::string ollama_url = "http://ollama:11434";
  std::string ollama_model = "llama3.2:1b";
  int ollama_timeout_ms = 30000;
  int ollama_health_ttl_ms = 5000;
  size_t ollama_max_queue = 16;
  size_t max_expanded_terms = 8;
  double expansion_weight = 0.5;  // BM25 weight of LLM-added terms

  static Config FromEnv() {
    Config c;
    Read("DATA_DIR", c.data_dir);
    Read("WEB_DIR", c.web_dir);
    Read("PORT", c.port);
    Read("HTTP_THREADS", c.http_threads);
    Read("INDEX_SEGMENTS", c.index_segments);
    Read("INDEX_BATCH_DOCS", c.index_batch_docs);
    Read("SEARCH_TOP_K", c.top_k);
    Read("SNIPPET_RADIUS", c.snippet_radius);
    Read("OLLAMA_URL", c.ollama_url);
    Read("OLLAMA_MODEL", c.ollama_model);
    Read("OLLAMA_TIMEOUT_MS", c.ollama_timeout_ms);
    Read("OLLAMA_HEALTH_TTL_MS", c.ollama_health_ttl_ms);
    Read("OLLAMA_MAX_QUEUE", c.ollama_max_queue);
    Read("MAX_EXPANDED_TERMS", c.max_expanded_terms);
    Read("EXPANSION_WEIGHT", c.expansion_weight);
    if (c.default_threads == 0) c.default_threads = 4;
    return c;
  }

 private:
  template <class T>
  static void Read(const char* name, T& out) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') return;
    if constexpr (std::is_same_v<T, std::string>) {
      out = value;
    } else if constexpr (std::is_floating_point_v<T>) {
      out = std::strtod(value, nullptr);
    } else {
      out = static_cast<T>(std::strtoll(value, nullptr, 10));
    }
  }
};

}  // namespace app