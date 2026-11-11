#include "index/inverted_index.h"

#include <algorithm>

#include "concurrency/wait_group.h"

namespace indexing {

InvertedIndex::InvertedIndex(size_t segments, size_t documents)
    : terms_(segments, /*capacity=*/segments * 64) {
  docs_.Resize(documents);
}

void InvertedIndex::AddDocument(uint32_t doc, DocInfo info,
                                const std::vector<std::pair<std::string, uint32_t>>& term_freqs) {
  total_tokens_.fetch_add(info.length_tokens, std::memory_order_relaxed);
  docs_.Set(doc, std::move(info));
  for (const auto& [term, tf] : term_freqs) {
    terms_.Upsert(term, [&](std::vector<Posting>& postings) { postings.push_back({doc, tf}); });
  }
}

void InvertedIndex::Finalize(concurrency::ThreadPool& pool) {
  concurrency::WaitGroup wg;
  for (size_t s = 0; s < terms_.segment_count(); ++s) {
    wg.Add(1);
    pool.Submit([this, s, &wg] {
      terms_.ForEachInSegment(s, [](std::string_view, std::vector<Posting>& postings) {
        std::sort(postings.begin(), postings.end(),
                  [](const Posting& a, const Posting& b) { return a.doc < b.doc; });
        postings.shrink_to_fit();
      });
      wg.Done();
    });
  }
  wg.Wait();
  const size_t n = docs_.size();
  avg_doc_length_ = n ? static_cast<double>(total_tokens_.load()) / n : 0;
}

}  // namespace indexing