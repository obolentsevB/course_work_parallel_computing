#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "index/inverted_index.h"

namespace indexing {

// Builds an InvertedIndex from all *.txt files under data_dir using its own
// ThreadPool of `threads` workers. Files are split into batches; every batch is
// one pool task that reads, tokenizes and inserts its documents.
class IndexBuilder {
 public:
  IndexBuilder(std::filesystem::path data_dir, size_t segments, size_t batch_docs)
      : data_dir_(std::move(data_dir)), segments_(segments), batch_docs_(batch_docs) {}

  std::shared_ptr<InvertedIndex> Build(size_t threads) const;

 private:
  std::vector<std::string> ScanFiles() const;
  void ProcessBatch(InvertedIndex& index, const std::vector<std::string>& files, size_t first,
                    size_t last) const;

  std::filesystem::path data_dir_;
  size_t segments_;
  size_t batch_docs_;
};

}  // namespace indexing