#pragma once

#include "../Types.inl"
#include <string>

class ILogger;

namespace osrv
{
	struct ServerConfigs;

	namespace ptz
	{
		extern const std::string CONFIGS_FILE;

		void init_service(HttpServer& srv, osrv::ServerConfigs& /*configs*/,
			const std::string& /*configs_path*/, ILogger& /*logger*/);
	}
}