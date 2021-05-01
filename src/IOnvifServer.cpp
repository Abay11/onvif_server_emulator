#include "../include/IOnvifServer.h"

#include "../onvif_services/IOnvifService.h"

#include "../onvif_services/recording_search_service.h"

namespace osrv
{

	std::shared_ptr<IOnvifService> OnvifServiceFactory::Create(const std::string& service_uri,
		const std::string & configs_path,
		std::shared_ptr<HttpServer> srv,
		std::shared_ptr<osrv::ServerConfigs> server_configs,
		std::shared_ptr<ILogger> log)
	{
		if (SERVICE_URI::SEARCH == service_uri)
		{
			return std::make_shared<RecordingSearchService>("Recording Search", configs_path, srv, server_configs, log);
		}

		throw std::runtime_error("Unknown service URI!");
	}

	std::shared_ptr<IOnvifService> IOnvifServer::DeviceService()
	{
		return std::shared_ptr<IOnvifService>();
	}

	std::shared_ptr<IOnvifService> IOnvifServer::RecordingSearchService()
	{
		if (!recording_search_service_)
		{
			recording_search_service_ = OnvifServiceFactory()
				.Create(SERVICE_URI::SEARCH, configs_path_, http_server_, server_configs_, logger_);
		}

		return recording_search_service_;
	}

}