#include "search/searcher.h"

#include <algorithm>
#include <cmath>

namespace search {

std::vector<Hit> Searcher::Search(const indexing::InvertedIndex& index,
                                  const std::vector<WeightedTerm>& terms, size_t k) const {
  const size_t n = index.doc_count();
  if (n == 0 || terms.empty()) return {};
  const double avg_len = std::max(index.avg_doc_length(), 1.0);

  // Dense accumulator: one score per document, local to this request.
  std::vector<double> scores(n, 0.0);
  std::vector<uint32_t> touched;

  for (const auto& [term, weight] : terms) {
    index.Lookup(term, [&](const std::vector<indexing::Posting>& postings) {
      const double df = static_cast<double>(postings.size());
      const double idf = std::log(1.0 + (n - df + 0.5) / (df + 0.5));
      for (const auto& [doc, tf] : postings) {
        const double len = index.doc(doc).length_tokens;
        const double norm = tf + k1_ * (1.0 - b_ + b_ * len / avg_len);
        if (scores[doc] == 0.0) touched.push_back(doc);
        scores[doc] += weight * idf * tf * (k1_ + 1.0) / norm;
      }
    });
  }

  std::vector<Hit> hits;
  hits.reserve(touched.size());
  for (uint32_t doc : touched) hits.push_back({doc, scores[doc]});

  // Top-k by score; ties broken by doc id so results are deterministic.
  const size_t top = std::min(k, hits.size());
  std::partial_sort(hits.begin(), hits.begin() + top, hits.end(), [](const Hit& a, const Hit& b) {
    return a.score != b.score ? a.score > b.score : a.doc < b.doc;
  });
  hits.resize(top);
  return hits;
}

}  // namespace search