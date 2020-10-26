#include <iostream>

#include "Logger.hpp"
#include "Server.h"

//#include <http_parser/http_parser.h>
static const std::string SERVER_VERSION = "0.1";

int main()

static const std::string DEFAULT_CONFIGS_DIR = "./server_configs";

int main(int argc, char** argv)
{
	using namespace std;

	Logger log(Logger::LVL_DEBUG);
	
	log.Info("Simple ONVIF Server ver. " + SERVER_VERSION);

	std::string configs_dir = DEFAULT_CONFIGS_DIR;

	if (argc > 1)
	{
		configs_dir = argv[1];
		log.Info("Command line arguments are found. "
			"Configuration files will be read from " + configs_dir);
	}

	try
	{
		osrv::Server server(configs_dir, log);
		server.run();
	}
	catch (const std::exception& e)
	{
		std::string msg = "Issues with the Server's work: ";
		msg += e.what();
		log.Error(msg);
	}

	log.Info("Server is stopped");

	cout << "\n\nPress any key to exit";
	getchar();
}
