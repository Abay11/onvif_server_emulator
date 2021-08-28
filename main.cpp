#include <iostream>

#include "Logger.h"
#include "Server.h"
#include "LoggerFactories.h"

static const std::string SERVER_VERSION = "0.1";

static const std::string DEFAULT_CONFIGS_DIR = "./server_configs";

static const std::string LOG_FILENAME = "OnvifServer.log";

int main(int argc, char** argv)
{
	using namespace std;
	std::cout << "ONVIF Server Emulator v" + SERVER_VERSION;

	std::shared_ptr<ILogger> logger{ FileLoggerFactory(LOG_FILENAME).GetLogger(ILogger::LVL_DEBUG) };

	std::string configs_dir = DEFAULT_CONFIGS_DIR;

	logger->Info("New run");

	if (argc > 1)
	{
		configs_dir = argv[1];
		logger->Info("Command line arguments are found. "
			"Configuration files will be read from " + configs_dir);
	}

	try
	{
		//std::shared_ptr<osrv::IOnvifServer> server = std::make_shared<osrv::Server>(configs_dir, logger);
		std::shared_ptr<osrv::Server> server{ new osrv::Server{configs_dir, logger} };
		server->init();
		server->run();
	}
	catch (const std::exception& e)
	{
		std::string msg = "Issues with the Server's work: ";
		msg += e.what();
		logger->Error(msg);
	}

	logger->Info("Server is stopped");

	cout << "\n\nPress any key to exit";
	getchar();
}
