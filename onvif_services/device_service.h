#pragma once

#include "../Types.inl"

class Logger;

namespace osrv
{
	namespace device
	{
		void init_service(HttpServer& srv, Logger& logger);
	}
}