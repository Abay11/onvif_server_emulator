#include "IOnvifService.h"

#include "../Logger.h"

#include "../include/IOnvifServer.h"
#include "../Simple-Web-Server/server_http.hpp"

#include "onvif_services/service_configs.h"

#include "device_service.h"

#include <boost/property_tree/ptree.hpp>

osrv::IOnvifService::IOnvifService(
	const std::string& service_uri,
	const std::string& service_name,
	std::shared_ptr<IOnvifServer> srv)
		:
		service_uri_(service_uri)
		, service_name_(service_name)
		, configs_ptree_ {ServiceConfigs(service_name, srv->ConfigsPath())}
		, onvif_server_(srv)
		, http_server_(srv->HttpServer())
		, server_configs_(srv->ServerConfigs())
		, log_(srv->Logger())
{
}

void osrv::IOnvifService::Run()
{
	if (is_running_)
		return;

	log_->Info("Running " + service_name_ + " service...");

	// TODO: uncomment and use this after DeviceService implementation as OnvifService
	//const auto dvc_srv_cfg = onvif_server_->DeviceService()->configs_ptree_;;

	auto dvc_srv_cfg = device::get_configs_tree_instance();
	const auto service_config = dvc_srv_cfg.find("GetServices");

	const auto uri = service_uri_;
	auto search_configs = std::find_if(service_config->second.begin(), service_config->second.end(),
		[uri](const pt::ptree::iterator::value_type tree) { return tree.second.get<std::string>("namespace") == uri; });

	std::string search_xaddr = "/" + search_configs->second.get<std::string>("XAddr");

	http_server_->resource[search_xaddr]["POST"] = [this](std::shared_ptr<HttpServer::Response> response,
		std::shared_ptr<HttpServer::Request> request) {
			handleRequestImpl(response, request->content);
	};

	is_running_ = true;
}
