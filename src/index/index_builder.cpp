#include "index/index_builder.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <unordered_map>

#include "concurrency/thread_pool.h"
#include "concurrency/wait_group.h"
#include "text/tokenizer.h"

namespace indexing {

namespace fs = std::filesystem;

std::shared_ptr<InvertedIndex> IndexBuilder::Build(size_t threads) const {
  const auto started = std::chrono::steady_clock::now();

  const std::vector<std::string> files = ScanFiles();
  auto index = std::make_shared<InvertedIndex>(segments_, files.size());

  concurrency::ThreadPool pool(threads);
  concurrency::WaitGroup wg;
  for (size_t first = 0; first < files.size(); first += batch_docs_) {
    const size_t last = std::min(first + batch_docs_, files.size());
    wg.Add(1);
    pool.Submit([&, first, last] {
      ProcessBatch(*index, files, first, last);
      wg.Done();
    });
  }
  wg.Wait();
  index->Finalize(pool);
  pool.Shutdown();

  const auto elapsed = std::chrono::steady_clock::now() - started;
  index->set_stats({files.size(),
                    std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(),
                    pool.worker_count()});
  return index;
}

std::vector<std::string> IndexBuilder::ScanFiles() const {
  std::vector<std::string> files;
  if (!fs::exists(data_dir_)) return files;
  for (const auto& entry : fs::recursive_directory_iterator(data_dir_)) {
    if (entry.is_regular_file() && entry.path().extension() == ".txt") {
      files.push_back(fs::relative(entry.path(), data_dir_).generic_string());
    }
  }
  // Sorted order makes doc ids deterministic between builds.
  std::sort(files.begin(), files.end());
  return files;
}

void IndexBuilder::ProcessBatch(InvertedIndex& index, const std::vector<std::string>& files,
                                size_t first, size_t last) const {
  // Thread-local state: no synchronization until AddDocument.
  std::unordered_map<std::string, uint32_t> counts;
  std::vector<std::pair<std::string, uint32_t>> term_freqs;
  std::string content;

  for (size_t doc = first; doc < last; ++doc) {
    std::ifstream in(data_dir_ / files[doc], std::ios::binary);
    content.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());

    counts.clear();
    uint32_t length = 0;
    text::Tokenizer::Tokenize(content, [&](std::string_view term, size_t) {
      ++counts[std::string(term)];
      ++length;
    });

    term_freqs.assign(counts.begin(), counts.end());
    index.AddDocument(static_cast<uint32_t>(doc), {files[doc], length}, term_freqs);
  }
}

}  // namespace indexing