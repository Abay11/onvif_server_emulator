#include <iostream>

#include "Logger.h"
#include "LoggerFactories.h"
#include "Server.h"

#include "onvif_services/service_configs.h"

#include <cerrno>
#include <climits>
#include <linux/close_range.h>
#include <sys/syscall.h>
#include <system_error>
#include <unistd.h>

#include <cstdlib>
#include <string>

static const std::string TITLE = R"(
   ___  _   ___     _____ _____   ____                             _____                 _       _             
  / _ \| \ | \ \   / /_ _|  ___| / ___|  ___ _ ____   _____ _ __  | ____|_ __ ___  _   _| | __ _| |_ ___  _ __ 
 | | | |  \| |\ \ / / | || |_    \___ \ / _ \ '__\ \ / / _ \ '__| |  _| | '_ ` _ \| | | | |/ _` | __/ _ \| '__|
 | |_| | |\  | \ V /  | ||  _|    ___) |  __/ |   \ V /  __/ |    | |___| | | | | | |_| | | (_| | || (_) | |   
  \___/|_| \_|  \_/  |___|_|     |____/ \___|_|    \_/ \___|_|    |_____|_| |_| |_|\__,_|_|\__,_|\__\___/|_|   
)";

static const std::string SERVER_VERSION = "0.1";

static const std::string DEFAULT_CONFIGS_DIR = "./server_configs";

static const std::string LOG_FILENAME = "OnvifServer.log";

int main(int argc, char** argv)
{
	using namespace std;
	std::cout << TITLE << std::endl;
	std::cout << "Application version: " << SERVER_VERSION << std::endl;

	std::string configs_dir = DEFAULT_CONFIGS_DIR;

	std::stringstream ss;

	if (argc > 1)
	{
		configs_dir = argv[1];
		ss << "Command line arguments are found. Configs read from " << configs_dir;
	}

	std::shared_ptr<boost::property_tree::ptree> serverConfigs = osrv::ServiceConfigs("common", configs_dir);
	std::shared_ptr<ILogger> logger;
	LoggerConfigs lconfigs(serverConfigs);
	if (lconfigs.LogOutput() == "console")
	{
		logger.reset(ConsoleLoggerFactory().GetLogger(lconfigs.LogLevel()));
	}
	else if (lconfigs.LogOutput() == "file")
	{
		logger.reset(FileLoggerFactory(LOG_FILENAME).GetLogger(lconfigs.LogLevel()));
	}
	else
		throw std::runtime_error("Could not create logger type: " + lconfigs.LogOutput());

	logger->Info("New run. " + ss.str());
	logger->Info("Logging level: " + logger->GetLogLevel());

	try
	{
		// std::shared_ptr<osrv::IOnvifServer> server = std::make_shared<osrv::Server>(configs_dir, logger);
		std::shared_ptr<osrv::Server> server{std::make_shared<osrv::Server>(configs_dir, logger)};
		server->init([argv, logger]() {
			logger->Info("SystemReboot: restarting application and reloading configuration");
			// Isolate this thread's descriptor table before marking all non-stdio FDs.
			// This also covers sockets opened concurrently by GStreamer and event workers.
			if (syscall(SYS_close_range, 3u, UINT_MAX, CLOSE_RANGE_UNSHARE | CLOSE_RANGE_CLOEXEC) != 0)
				throw std::system_error(errno, std::generic_category(), "SystemReboot close_range");
			execv("/proc/self/exe", argv);
			throw std::system_error(errno, std::generic_category(), "SystemReboot execv");
		});
		server->run();
	}
	catch (const std::exception& e)
	{
		std::string msg = "Issues with the Server's work: ";
		msg += e.what();
		logger->Error(msg);
		return EXIT_FAILURE;
	}

	logger->Info("Server is stopped");

}
