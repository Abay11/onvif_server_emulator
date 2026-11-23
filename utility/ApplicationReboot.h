#pragma once

#include "../HttpServerFwd.h"
#include "../HttpServer.h"
#include <memory>

class ILogger;
namespace osrv
{
struct ServerConfigs;
// The caller must finish writing the response before invoking this function.
// SOAP and WebUI share one pending flag so only one restart is scheduled.
void RebootAfterResponse(const std::shared_ptr<HttpServer::Response>& response,
												 const std::shared_ptr<ServerConfigs>& configs, const std::shared_ptr<ILogger>& logger);
} // namespace osrv
