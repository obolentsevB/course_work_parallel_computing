#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace search {

// Cuts a short fragment of the document around the first matching term.
class SnippetExtractor {
 public:
  SnippetExtractor(std::filesystem::path data_dir, size_t radius)
      : data_dir_(std::move(data_dir)), radius_(radius) {}

  std::string Extract(const std::string& rel_path, const std::vector<std::string>& terms) const;

 private:
  std::filesystem::path data_dir_;
  size_t radius_;
};

}  // namespace search