#include <iostream>

#include "Logger.h"
#include "Server.h"
#include "LoggerFactories.h"

static const std::string SERVER_VERSION = "0.1";

static const std::string DEFAULT_CONFIGS_DIR = "./server_configs";

int main(int argc, char** argv)
{
	using namespace std;

	ILogger* logger = ConsoleLoggerFactory().GetLogger(ILogger::LVL_DEBUG);
	logger->Info("Simple ONVIF Server ver. " + SERVER_VERSION);

	std::string configs_dir = DEFAULT_CONFIGS_DIR;

	if (argc > 1)
	{
		configs_dir = argv[1];
		logger->Info("Command line arguments are found. "
			"Configuration files will be read from " + configs_dir);
	}

	try
	{
		osrv::Server* server = new osrv::Server(configs_dir, *logger);
		server->run();

		delete server;
	}
	catch (const std::exception& e)
	{
		std::string msg = "Issues with the Server's work: ";
		msg += e.what();
		logger->Error(msg);
	}

	logger->Info("Server is stopped");

	delete logger;

	cout << "\n\nPress any key to exit";
	getchar();
}
