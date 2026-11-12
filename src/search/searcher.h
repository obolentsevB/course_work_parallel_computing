#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "index/inverted_index.h"

namespace search {

struct WeightedTerm {
  std::string term;
  double weight = 1.0;
};

struct Hit {
  uint32_t doc;
  double score;
};

// Ranks documents with BM25. Stateless, so one instance is shared by all
// request threads; it only reads the immutable index snapshot.
class Searcher {
 public:
  std::vector<Hit> Search(const indexing::InvertedIndex& index,
                          const std::vector<WeightedTerm>& terms, size_t k) const;

 private:
  double k1_ = 1.2;
  double b_ = 0.75;
};

}  // namespace search