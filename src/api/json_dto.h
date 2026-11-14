#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

#include "index/inverted_index.h"

namespace api {

// Builders for the fixed JSON contract of the REST/WebSocket API.
// ordered_json keeps fields in the documented order.
namespace dto {

using Json = nlohmann::ordered_json;

inline Json Health(bool ollama) { return {{"status", "ok"}, {"ollama", ollama}}; }

inline Json Index(size_t documents, int64_t time_ms) {
  return {{"status", "ok"}, {"documents_indexed", documents}, {"time_ms", time_ms}};
}

inline Json Result(const std::string& doc_id, double score, const std::string& snippet) {
  return {{"doc_id", doc_id}, {"score", score}, {"snippet", snippet}};
}

inline Json Search(Json results, int64_t time_ms) {
  return {{"results", std::move(results)}, {"time_ms", time_ms}};
}

inline Json Stats(const indexing::IndexStats& s) {
  return {{"documents", s.documents},
          {"index_build_time_ms", s.build_time_ms},
          {"threads_used", s.threads_used}};
}

inline Json Stage(const std::string& stage, Json data) {
  Json message = {{"stage", stage}};
  if (!data.is_null()) message["data"] = std::move(data);
  return message;
}

inline Json Error(const std::string& message) {
  return {{"status", "error"}, {"message", message}};
}

}  // namespace dto
}  // namespace api