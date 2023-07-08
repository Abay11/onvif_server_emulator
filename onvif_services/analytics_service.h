#pragma once

#include "IOnvifService.h"

#include "../HttpServerFwd.h"

#include <string>

class ILogger;

namespace osrv
{
class AnalyticsService : public IOnvifService
{
public:
	AnalyticsService(const std::string& service_uri, const std::string& service_name, std::shared_ptr<IOnvifServer> srv);
};
} // namespace osrv