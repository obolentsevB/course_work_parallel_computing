#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "concurrency/thread_pool.h"
#include "index/concurrent_hash_map.h"

namespace indexing {

struct Posting {
  uint32_t doc;  // document id
  uint32_t tf;   // term frequency in that document
};

struct DocInfo {
  std::string rel_path;
  uint32_t length_tokens = 0;
};

// Document metadata indexed by doc id. Allocated once before the build, after
// that every worker writes only its own documents' slots, so no lock is needed.
class DocumentStore {
 public:
  void Resize(size_t n) { docs_.resize(n); }
  void Set(uint32_t doc, DocInfo info) { docs_[doc] = std::move(info); }
  const DocInfo& Get(uint32_t doc) const { return docs_[doc]; }
  size_t size() const { return docs_.size(); }

 private:
  std::vector<DocInfo> docs_;
};

struct IndexStats {
  size_t documents = 0;
  int64_t build_time_ms = 0;
  size_t threads_used = 0;
};

// term -> posting list. Built concurrently, then finalized and published as an
// immutable snapshot (shared_ptr<const InvertedIndex>).
class InvertedIndex {
 public:
  InvertedIndex(size_t segments, size_t documents);

  // Thread-safe. term_freqs: (term, tf) pairs of one document.
  void AddDocument(uint32_t doc, DocInfo info,
                   const std::vector<std::pair<std::string, uint32_t>>& term_freqs);

  // Sorts every posting list by doc id (one pool task per segment) and
  // computes the average document length. Must run after all AddDocument calls.
  void Finalize(concurrency::ThreadPool& pool);

  // Calls fn(const std::vector<Posting>&) if the term exists.
  template <class F>
  bool Lookup(std::string_view term, F&& fn) const {
    return terms_.Find(term, std::forward<F>(fn));
  }

  const DocInfo& doc(uint32_t id) const { return docs_.Get(id); }
  size_t doc_count() const { return docs_.size(); }
  size_t term_count() const { return terms_.Size(); }
  double avg_doc_length() const { return avg_doc_length_; }
  const IndexStats& stats() const { return stats_; }
  void set_stats(IndexStats stats) { stats_ = stats; }

 private:
  ConcurrentHashMap<std::vector<Posting>> terms_;
  DocumentStore docs_;
  std::atomic<uint64_t> total_tokens_{0};
  double avg_doc_length_ = 0;
  IndexStats stats_;
};

}  // namespace indexing