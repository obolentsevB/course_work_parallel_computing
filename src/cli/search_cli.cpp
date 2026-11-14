// Command-line client.
//   search "query"            keyword search over REST (POST /api/search)
//   search --ws "query"       full pipeline over WebSocket (/ws/search)
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

  int SearchWebSocket(const std::string& query) {
    std::string ws_url = url_;
    ws_url.replace(0, ws_url.find("://"), ws_url.rfind("https", 0) == 0 ? "wss" : "ws");
    httplib::ws::WebSocketClient ws(ws_url + "/ws/search");
    ws.set_read_timeout(std::chrono::seconds(120));
    if (!ws.connect()) return Fail("cannot open WebSocket at " + ws_url);
    ws.send(json{{"query", query}}.dump());

    std::string message;
    while (ws.read(message) == httplib::ws::ReadResult::Text) {
      const json m = json::parse(message, nullptr, false);
      const std::string stage = m.value("stage", "");
      std::cout << "[" << stage << "]\n";
      if (stage == "query_expansion") {
        std::cout << "   expanded terms: " << m["data"]["expanded_terms"].dump() << "\n";
      } else if (stage == "keyword_search") {
        PrintResults(m["data"]["results"]);
        std::cout << "(" << m["data"]["time_ms"] << " ms)\n";
      } else if (stage == "error") {
        return Fail(m["data"].value("message", ""));
      } else if (stage == "done") {
        break;
      }
    }
    ws.close();
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

  const std::string usage = "usage: search [--ws] \"query\" | search --index [threads]";
  if (argc < 2) return Fail(usage);
  const std::string first = argv[1];
  if (first == "--index") return cli.BuildIndex(argc > 2 ? std::atoi(argv[2]) : 0);
  if (first == "--ws") return argc > 2 ? cli.SearchWebSocket(argv[2]) : Fail(usage);
  return cli.SearchRest(first);
}