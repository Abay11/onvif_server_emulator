#include "IOnvifService.h"

#include "../Logger.h"

#include "onvif_services/service_configs.h"

osrv::IOnvifService::IOnvifService(
	const std::string& service_name,
	const std::string& configs_path,
	std::shared_ptr<HttpServer> srv,
	std::shared_ptr<osrv::ServerConfigs> server_configs,
	std::shared_ptr<ILogger> log)
		:
		service_name_(service_name)
		, configs_ptree_ {ServiceConfigs(service_name, configs_path)}
		, http_server_(srv)
		, server_configs_(server_configs)
		, log_(log)
	{
	}

void osrv::IOnvifService::Run()
{
	log_->Info("Running " + service_name_ + " service...");
}
