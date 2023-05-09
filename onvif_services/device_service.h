#pragma once

#include "IOnvifService.h"

namespace osrv
{
class DeviceService : public IOnvifService
{
public:
	DeviceService(const std::string& service_uri, const std::string& service_name, std::shared_ptr<IOnvifServer> srv);
};
} // namespace osrv