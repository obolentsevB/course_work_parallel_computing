#include "search/query_expander.h"

#include <algorithm>
#include <nlohmann/json.hpp>

#include "text/tokenizer.h"

namespace search {

using json = nlohmann::json;

std::vector<std::string> QueryExpander::Expand(const std::string& query,
                                               llm::Clock::time_point deadline) const {
  if (!ollama_.IsAvailable()) return {};
  const auto raw = ollama_.Generate(BuildPrompt(query), deadline);
  if (!raw) return {};

  std::vector<std::string> original;
  text::Tokenizer::Tokenize(query, [&](std::string_view t, size_t) { original.emplace_back(t); });
  return Sanitize(*raw, original);
}

std::string QueryExpander::BuildPrompt(const std::string& query) {
  return "You expand search queries for a keyword search engine over English texts.\n"
         "Return JSON of the form {\"terms\": [\"word\", ...]} with up to 8 single "
         "English words: important keywords of the query and their synonyms or closely "
         "related words. No explanations.\n"
         "Query: " +
         query;
}

std::vector<std::string> QueryExpander::Sanitize(const std::string& raw,
                                                 const std::vector<std::string>& original) const {
  const json answer = json::parse(raw, nullptr, /*allow_exceptions=*/false);
  if (!answer.is_object() || !answer.contains("terms") || !answer["terms"].is_array()) return {};

  std::vector<std::string> terms;
  auto seen = [&](std::string_view t) {
    return std::find(original.begin(), original.end(), t) != original.end() ||
           std::find(terms.begin(), terms.end(), t) != terms.end();
  };
  for (const auto& item : answer["terms"]) {
    if (!item.is_string()) continue;
    // The model may return phrases: split them with the same tokenizer as the index.
    text::Tokenizer::Tokenize(item.get<std::string>(), [&](std::string_view t, size_t) {
      if (terms.size() < max_terms_ && !seen(t)) terms.emplace_back(t);
    });
  }
  return terms;
}

}  // namespace search