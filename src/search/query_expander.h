#pragma once

#include <string>
#include <vector>

#include "llm/ollama_client.h"

namespace search {

// Query expansion: asks the LLM for keywords and synonyms related to the query
// and turns its answer into clean index terms.
class QueryExpander {
 public:
  QueryExpander(llm::OllamaClient& ollama, size_t max_terms)
      : ollama_(ollama), max_terms_(max_terms) {}

  // Returns only the *added* terms (original query terms are excluded).
  // On any failure (Ollama down, timeout, bad JSON) returns an empty list.
  std::vector<std::string> Expand(const std::string& query, llm::Clock::time_point deadline) const;

 private:
  static std::string BuildPrompt(const std::string& query);
  std::vector<std::string> Sanitize(const std::string& raw,
                                    const std::vector<std::string>& original) const;

  llm::OllamaClient& ollama_;
  size_t max_terms_;
};

}  // namespace search