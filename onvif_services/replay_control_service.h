#pragma once

#include "IOnvifService.h"

class ILogger;

namespace osrv
{
	class ReplayControlService : public IOnvifService
	{
	public:
		ReplayControlService(const std::string& service_uri,
			const std::string& service_name, std::shared_ptr<IOnvifServer> srv);

	};
}