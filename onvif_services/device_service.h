#pragma once

#include "../Types.inl"

class Logger;

namespace osrv
{
	namespace device
	{
		void init_service(HttpServer& srv, DigestSessionSP /*digest_session*/,
			const std::string& /*configs_path*/, Logger& /*logger*/);
	}
}