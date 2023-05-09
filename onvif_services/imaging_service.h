#pragma once

#include "IOnvifService.h"

#include "../HttpServerFwd.h"

#include <string>

class ILogger;

namespace osrv
{
struct ServerConfigs;

class ImagingService : public IOnvifService
{
public:
	ImagingService(const std::string& service_uri, const std::string& service_name, std::shared_ptr<IOnvifServer> srv);
};
} // namespace osrv