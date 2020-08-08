#include <iostream>

#include "Logger.hpp"
#include "Server.h"

//#include <http_parser/http_parser.h>
#include <boost/asio.hpp>

int main()
{
	using namespace std;

	using namespace boost::asio;

	Logger log(Logger::LVL_DEBUG);

	//// STARTING SERVER
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
