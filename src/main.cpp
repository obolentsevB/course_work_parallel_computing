#include <iostream>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include "app/config.h"

int main() {
  const app::Config config = app::Config::FromEnv();
  httplib::Server server;
  server.Get("/api/health", [](const httplib::Request&, httplib::Response& res) {
    res.set_content(nlohmann::json{{"status", "ok"}, {"ollama", false}}.dump(), "application/json");
  });
  std::cout << "listening on :" << config.port << std::endl;
  return server.listen("0.0.0.0", config.port) ? 0 : 1;
}