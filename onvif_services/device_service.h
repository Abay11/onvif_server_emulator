#pragma once

#include "../Types.inl"

class ILogger;

namespace osrv
{
	struct ServerConfigs;

	namespace device
	{
		extern const std::string CONFIGS_FILE;

		void init_service(HttpServer& srv, osrv::ServerConfigs& /*configs*/,
			const std::string& /*configs_path*/, ILogger& /*logger*/);
	}
}