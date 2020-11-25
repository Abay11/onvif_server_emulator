#pragma once

#include <string>

class Logger;

namespace osrv
{
	namespace discovery
	{
		// should be called before before start
		void init_service(const std::string& /*configs_path*/, Logger& /*logger*/);

		// may throw an exception if called before @init
		void start();

		
		void stop();
	}
}