#pragma once

#include "../HttpServerFwd.h"

#include <string>

class ILogger;

namespace osrv
{
struct ServerConfigs;

namespace event
{
void init_service(HttpServer& /*srv*/, const osrv::ServerConfigs& /*configs*/, const std::string& /*configs_path*/,
									ILogger& /*logger*/);
}
} // namespace osrv