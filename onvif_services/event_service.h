#pragma once

#include "../Types.inl"

class Logger;

namespace osrv
{
	struct ServerConfigs;

	namespace event
	{
		void init_service(HttpServer& /*srv*/, const osrv::ServerConfigs& /*configs*/,
			const std::string& /*configs_path*/, Logger& /*logger*/);
	}
}