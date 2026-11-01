#include <httplib.h>
#include <nlohmann/json.hpp>

int main() {
  httplib::Server server;
  server.Get("/api/health", [](const httplib::Request&, httplib::Response& res) {
    res.set_content(nlohmann::json{{"status", "ok"}, {"ollama", false}}.dump(), "application/json");
  });
  return server.listen("0.0.0.0", 8080) ? 0 : 1;
}