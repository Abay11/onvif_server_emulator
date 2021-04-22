#pragma once

#include "../Types.inl"

class ILogger;

namespace osrv
{
	struct ServerConfigs;

	namespace recording_search
	{
		static const std::string NS = "https://www.onvif.org/ver10/search.wsdl";

		void init_service(HttpServer& /*srv*/, const osrv::ServerConfigs& /*configs*/,
			const std::string& /*configs_path*/, ILogger& /*logger*/);
	}
}
