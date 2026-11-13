// Command-line client.
//   search "query"            keyword search over REST (POST /api/search)
//   search --index [threads]  build the index (POST /api/index)
// Server address: SEARCH_URL environment variable (default http://localhost:8080).

#include <httplib.h>

#include <cstdlib>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>

using json = nlohmann::json;

namespace {

void PrintResults(const json& results) {
  if (results.empty()) std::cout << "No results.\n";
  int rank = 1;
  for (const auto& r : results) {
    std::cout << rank++ << ". " << r["doc_id"].get<std::string>() << "  (score "
              << r["score"].get<double>() << ")\n   " << r["snippet"].get<std::string>() << "\n";
  }
}

int Fail(const std::string& message) {
  std::cerr << "error: " << message << "\n";
  return 1;
}

class SearchCli {
 public:
  explicit SearchCli(std::string url) : url_(std::move(url)) {}

  int SearchRest(const std::string& query) {
    httplib::Client client(url_);
    client.set_read_timeout(std::chrono::seconds(60));
    const json body = {{"query", query}, {"mode", "keyword"}};
    auto res = client.Post("/api/search", body.dump(), "application/json");
    if (!res) return Fail("server is not reachable at " + url_);
    const json answer = json::parse(res->body, nullptr, false);
    if (res->status != 200) return Fail(answer.value("message", res->body));
    PrintResults(answer["results"]);
    std::cout << "(" << answer["time_ms"] << " ms)\n";
    return 0;
  }

  int BuildIndex(int threads) {
    httplib::Client client(url_);
    client.set_read_timeout(std::chrono::hours(1));
    json body = json::object();
    if (threads > 0) body["threads"] = threads;
    auto res = client.Post("/api/index", body.dump(), "application/json");
    if (!res) return Fail("server is not reachable at " + url_);
    std::cout << res->body << "\n";
    return res->status == 200 ? 0 : 1;
  }

 private:
  std::string url_;
};

}  // namespace

int main(int argc, char** argv) {
  const char* env_url = std::getenv("SEARCH_URL");
  SearchCli cli(env_url ? env_url : "http://localhost:8080");

  const std::string usage = "usage: search \"query\" | search --index [threads]";
  if (argc < 2) return Fail(usage);
  const std::string first = argv[1];
  if (first == "--index") return cli.BuildIndex(argc > 2 ? std::atoi(argv[2]) : 0);
  return cli.SearchRest(first);
}