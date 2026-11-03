#include <iostream>

#include "api/http_server.h"
#include "app/config.h"

int main() {
  api::HttpServer server(app::Config::FromEnv());
  if (!server.Run()) {
    std::cerr << "failed to start the server" << std::endl;
    return 1;
  }
  return 0;
}