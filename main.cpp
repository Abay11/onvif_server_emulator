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
	const int CONCURRENCY_HINT = 1;
	boost::asio::io_context ctx_(CONCURRENCY_HINT);

	osrv::Server server(ctx_, log);
	try
	{
		server.run();
	}
	catch (const boost::system::system_error& e)
	{
		std::string msg = "Can't run Server ";
		msg += e.what();
		log.Error(msg);

		return -1;
	}

	log.Info("Server is stopped");
}
