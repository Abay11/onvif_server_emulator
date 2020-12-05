#pragma once

#include "../Types.inl"

class ILogger;

namespace osrv
{
	struct ServerConfigs;

	namespace device
	{
		void init_service(HttpServer& srv, const osrv::ServerConfigs& /*configs*/,
			const std::string& /*configs_path*/, ILogger& /*logger*/);
	}
}