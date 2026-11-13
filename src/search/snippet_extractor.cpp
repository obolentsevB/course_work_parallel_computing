#include "search/snippet_extractor.h"

#include <algorithm>
#include <fstream>
#include <iterator>

#include "text/tokenizer.h"

namespace search {

namespace {

bool IsContinuationByte(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

// Collapses whitespace and drops "<br />" tags (present in the IMDB dataset).
std::string Clean(std::string_view raw) {
  std::string out;
  for (size_t i = 0; i < raw.size(); ++i) {
    if (raw.compare(i, 6, "<br />") == 0) {
      i += 5;
      out.push_back(' ');
      continue;
    }
    const char c = raw[i];
    const bool space = c == ' ' || c == '\n' || c == '\r' || c == '\t';
    if (space && (out.empty() || out.back() == ' ')) continue;
    out.push_back(space ? ' ' : c);
  }
  return out;
}

}  // namespace

std::string SnippetExtractor::Extract(const std::string& rel_path,
                                      const std::vector<std::string>& terms) const {
  std::ifstream in(data_dir_ / rel_path, std::ios::binary);
  const std::string content{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};

  size_t hit = std::string::npos;
  text::Tokenizer::Tokenize(content, [&](std::string_view token, size_t pos) {
    if (hit == std::string::npos && std::find(terms.begin(), terms.end(), token) != terms.end()) {
      hit = pos;
    }
  });
  if (hit == std::string::npos) hit = 0;

  size_t begin = hit > radius_ ? hit - radius_ : 0;
  size_t end = std::min(content.size(), hit + radius_);
  // Never cut a multi-byte UTF-8 character in half.
  while (begin > 0 && IsContinuationByte(content[begin])) --begin;
  while (end < content.size() && IsContinuationByte(content[end])) ++end;

  std::string snippet = Clean(std::string_view(content).substr(begin, end - begin));
  if (begin > 0) snippet = "..." + snippet;
  if (end < content.size()) snippet += "...";
  return snippet;
}

}  // namespace search