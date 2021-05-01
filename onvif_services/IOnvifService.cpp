#include "IOnvifService.h"

#include "../Logger.h"

#include "../include/IOnvifServer.h"

#include "onvif_services/service_configs.h"

osrv::IOnvifService::IOnvifService(
	const std::string& service_name,
	std::shared_ptr<IOnvifServer> srv)
		:
		service_name_(service_name)
		, configs_ptree_ {ServiceConfigs(service_name, srv->ConfigsPath())}
		, onvif_server_(srv)
		, http_server_(srv->HttpServer())
		, server_configs_(srv->ServerConfigs())
		, log_(srv->Logger())
	{
	}

void osrv::IOnvifService::Run()
{
	log_->Info("Running " + service_name_ + " service...");


}
