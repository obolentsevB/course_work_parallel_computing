#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace text {

// Splits text into lowercase terms. A term is a run of ASCII letters/digits or
// non-ASCII bytes (so UTF-8 words such as Cyrillic stay intact).
class Tokenizer {
 public:
  static constexpr size_t kMinLength = 2;
  static constexpr size_t kMaxLength = 40;

  // Calls on_token(term, byte_offset_in_text) for every term that is not a
  // stop word. `term` is only valid during the callback.
  template <class F>
  static void Tokenize(std::string_view text, F&& on_token) {
    std::string term;
    size_t start = 0;
    for (size_t i = 0; i <= text.size(); ++i) {
      const bool inside = i < text.size() && IsTermByte(text[i]);
      if (inside) {
        if (term.empty()) start = i;
        if (term.size() < kMaxLength) term.push_back(ToLower(text[i]));
        continue;
      }
      if (term.size() >= kMinLength && !IsStopWord(term)) {
        on_token(std::string_view(term), start);
      }
      term.clear();
    }
  }

  static bool IsStopWord(std::string_view token) {
    static constexpr std::array<std::string_view, 48> kStopWords = {
        "a",    "an",   "and", "are",  "as",   "at",   "be",    "br",   "but",  "by",
        "for",  "from", "had", "has",  "have", "he",   "her",   "his",  "if",   "in",
        "into", "is",   "it",  "its",  "me",   "my",   "no",    "not",  "of",   "on",
        "or",   "she",  "so",  "than", "that", "the",  "their", "them", "then", "there",
        "they", "this", "to",  "was",  "were", "what", "which", "with"};
    for (auto word : kStopWords) {
      if (word == token) return true;
    }
    return false;
  }

 private:
  static bool IsTermByte(char c) {
    const auto u = static_cast<unsigned char>(c);
    return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || u >= 0x80;
  }
  static char ToLower(char c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
};

}  // namespace text