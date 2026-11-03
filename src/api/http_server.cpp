#include <iostream>
#include "api/http_server.h"
#include "api/json_dto.h"

namespace api {

namespace {

using dto::Json;

void SendJson(httplib::Response& res, const Json& body, int status = 200) {
  res.status = status;
  res.set_content(body.dump(), "application/json");
}

class RestHandlers {
 public:
  explicit RestHandlers(Services& s) : s_(s) {}

  void Health(const httplib::Request&, httplib::Response& res) {
    SendJson(res, dto::Health(false));
  }

 private:
  Services& s_;
};

}  // namespace

Services::Services(const app::Config& c) : config(c) {}

HttpServer::HttpServer(const app::Config& config) : services_(std::make_unique<Services>(config)) {
  RegisterRoutes();
}

void HttpServer::RegisterRoutes() {
  // Handlers are stateless wrappers over Services, shared by all threads.
  auto rest = std::make_shared<RestHandlers>(*services_);

  server_.Get("/api/health", [rest](auto& req, auto& res) { rest->Health(req, res); });
}

bool HttpServer::Run() {
  const auto& c = services_->config;
  std::cout << "listening on :" << c.port << " (data: " << c.data_dir
            << ", ollama: " << c.ollama_url << " " << c.ollama_model << ")" << std::endl;
  return server_.listen("0.0.0.0", c.port);
}

}  // namespace api