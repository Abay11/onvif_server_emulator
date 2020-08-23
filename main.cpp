#include <iostream>

#include "Logger.hpp"
#include "Server.h"

//#include <http_parser/http_parser.h>
static const std::string SERVER_VERSION = "0.1";

int main()
{
	using namespace std;

	Logger log(Logger::LVL_DEBUG);
	
	log.Info("Simple ONVIF Server ver. " + SERVER_VERSION);

	osrv::Server server(log);
	try
	{
		server.run();
	}
	catch (const boost::system::system_error& e)
	{
		std::string msg = "Issues with the Server's work: ";
		msg += e.what();
		log.Error(msg);

		return -1;
	}

	log.Info("Server is stopped");
}
