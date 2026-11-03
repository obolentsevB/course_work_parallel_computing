#pragma once

#include <httplib.h>
#include <memory>
#include "app/config.h"

namespace api {

// Everything request handlers share. All members are thread-safe or immutable.
struct Services {
  explicit Services(const app::Config& config);

  app::Config config;
};

// REST server.
class HttpServer {
 public:
  explicit HttpServer(const app::Config& config);
  bool Run();
  void Stop() { server_.stop(); }

 private:
  void RegisterRoutes();

  std::unique_ptr<Services> services_;
  httplib::Server server_;
};

}  // namespace api