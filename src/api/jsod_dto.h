#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace api {

// Builders for the fixed JSON contract of the REST/WebSocket API.
// ordered_json keeps fields in the documented order.
namespace dto {

using Json = nlohmann::ordered_json;

inline Json Health(bool ollama) { return {{"status", "ok"}, {"ollama", ollama}}; }

inline Json Error(const std::string& message) {
  return {{"status", "error"}, {"message", message}};
}

}  // namespace dto
}  // namespace api