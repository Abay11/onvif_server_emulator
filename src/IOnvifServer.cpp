#include "../include/IOnvifServer.h"

#include "../onvif_services/IOnvifService.h"

#include "../Simple-Web-Server/server_http.hpp"

#include "../onvif_services/recording_search_service.h"

namespace osrv
{

	std::shared_ptr<IOnvifService> OnvifServiceFactory::Create(const std::string& service_uri,
		std::shared_ptr<IOnvifServer> srv)
	{
		if (SERVICE_URI::DEVICE == service_uri)
		{
			//return std::make_shared<DeviceService>(service_uri, "Recording Search", srv);
		}


		if (SERVICE_URI::SEARCH == service_uri)
		{
			return std::make_shared<RecordingSearchService>(service_uri, "Recording Search", srv);
		}

		throw std::runtime_error("Unknown service URI!");
	}

	IOnvifServer::IOnvifServer(const std::string& configs_path, std::shared_ptr<ILogger> logger)
		: logger_(logger)
		, configs_path_(configs_path)
		, http_server_(new osrv::HttpServer())
	{}

	const std::string& IOnvifServer::ConfigsPath() const
	{
		return configs_path_;
	}

	std::shared_ptr<IOnvifService> IOnvifServer::DeviceService()
	{
		if (!device_service_)
		{
			auto t = shared_from_this();
			recording_search_service_ = OnvifServiceFactory()
				.Create(SERVICE_URI::DEVICE, t);
		}

		return device_service_;
	}

	std::shared_ptr<IOnvifService> IOnvifServer::RecordingSearchService()
	{
		if (!recording_search_service_)
		{
			auto t = shared_from_this();
			recording_search_service_ = OnvifServiceFactory()
				.Create(SERVICE_URI::SEARCH, t);
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