#include "../include/IOnvifServer.h"

#include "../onvif_services/IOnvifService.h"

#include "../onvif_services/recording_search_service.h"

namespace osrv
{

	std::shared_ptr<IOnvifService> OnvifServiceFactory::Create(const std::string& service_uri,
		std::shared_ptr<IOnvifServer> srv)
	{
		if (SERVICE_URI::SEARCH == service_uri)
		{
			return std::make_shared<RecordingSearchService>("Recording Search", srv);
		}

		throw std::runtime_error("Unknown service URI!");
	}

	const std::string& IOnvifServer::ConfigsPath() const
	{
		return configs_path_;
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
				.Create(SERVICE_URI::SEARCH, shared_from_this());
		}

		return recording_search_service_;
	}

	const std::shared_ptr<ILogger> IOnvifServer::Logger() const
	{
		return logger_;
	}

	std::shared_ptr<HttpServer> IOnvifServer::HttpServer()
	{
		return http_server_;
	}

	std::shared_ptr<ServerConfigs> IOnvifServer::ServerConfigs()
	{
		return server_configs_;
	}

}