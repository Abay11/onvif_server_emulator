#pragma once

#include "IOnvifService.h"

namespace osrv
{
	class DeviceIOService : public IOnvifService
	{
	public:
		DeviceIOService(const std::string& service_uri,
			const std::string& service_name, std::shared_ptr<IOnvifServer> srv);
	};
}