#include "../include/IOnvifServer.h"

#include "../utility/AuthHelper.h"
#include "../utility/MediaProfilesManager.h"

#include "../Server.h"

#include "../onvif_services/IOnvifService.h"

#include "../Simple-Web-Server/server_http.hpp"

#include "onvif_services/service_configs.h"

#include "../onvif_services/device_service.h"
#include "../onvif_services/deviceio_service.h"
#include "../onvif_services/imaging_service.h"
#include "../onvif_services/media_service.h"
#include "../onvif_services/media2_service.h"
#include "../onvif_services/ptz_service.h"
#include "../onvif_services/recording_search_service.h"
#include "../onvif_services/replay_control_service.h"

static const std::string PROFILES_CONFIG_FILE{ "media_profiles" };

namespace osrv
{
	std::shared_ptr<IOnvifService> OnvifServiceFactory::Create(const std::string& service_uri,
		std::shared_ptr<IOnvifServer> srv)
	{
		if (SERVICE_URI::DEVICE == service_uri)
		{
			return std::make_shared<DeviceService>(service_uri, "Device", srv);
		}

		if (SERVICE_URI::DEVICEIO == service_uri)
		{
			return std::make_shared<DeviceIOService>(service_uri, "DeviceIO", srv);
		}

		if (SERVICE_URI::IMAGING == service_uri)
		{
			return std::make_shared<ImagingService>(service_uri, "Imaging", srv);
		}

		if (SERVICE_URI::MEDIA == service_uri)
		{
			return std::make_shared<MediaService>(service_uri, "Media", srv);
		}

		if (SERVICE_URI::MEDIA2 == service_uri)
		{
			return std::make_shared<Media2Service>(service_uri, "Media2", srv);
		}

		if (SERVICE_URI::PTZ == service_uri)
		{
			return std::make_shared<PTZService>(service_uri, "PTZ", srv);
		}

		if (SERVICE_URI::REPLAY == service_uri)
		{
			return std::make_shared<ReplayControlService>(service_uri, "Replay Control", srv);
		}

		if (SERVICE_URI::SEARCH == service_uri)
		{
			return std::make_shared<RecordingSearchService>(service_uri, "Recording Search", srv);
		}

		throw std::runtime_error("Unknown service URI!");
	}

	IOnvifServer::IOnvifServer(const std::string& configs_path, std::shared_ptr<ILogger> logger)
		: configs_path_(configs_path)
		, logger_(logger)
		, http_server_(new osrv::HttpServer())
		, media_profiles_manager_(std::make_shared<utility::media::MediaProfilesManager>(ConfigPath(configs_path_,
			ConfigName(PROFILES_CONFIG_FILE))))
	{}

	const std::string& IOnvifServer::ConfigsPath() const
	{
		return configs_path_;
	}

	std::shared_ptr<IOnvifService> IOnvifServer::DeviceService()
	{
		if (!device_service_)
		{
			device_service_ = OnvifServiceFactory()
				.Create(SERVICE_URI::DEVICE, shared_from_this());
		}

		return device_service_;
	}
	
	std::shared_ptr<IOnvifService> IOnvifServer::DeviceIOService()
	{
		if (!deviceio_service_)
		{
			deviceio_service_ = OnvifServiceFactory()
				.Create(SERVICE_URI::DEVICEIO, shared_from_this());
		}

		return deviceio_service_;
	}

	std::shared_ptr<IOnvifService> IOnvifServer::ImagingService()
	{
		if (!imaging_service_)
		{
			imaging_service_ = OnvifServiceFactory()
				.Create(SERVICE_URI::IMAGING, shared_from_this());
		}

		return imaging_service_;
	}

	std::shared_ptr<IOnvifService> IOnvifServer::MediaService()
	{
		if (!media_service_)
		{
			media_service_ = OnvifServiceFactory()
				.Create(SERVICE_URI::MEDIA, shared_from_this());
		}

		return media_service_;
	}

	std::shared_ptr<IOnvifService> IOnvifServer::Media2Service()
	{
		if (!media2_service_)
		{
			media2_service_ = OnvifServiceFactory()
				.Create(SERVICE_URI::MEDIA2, shared_from_this());
		}

		return media2_service_;
	}

	std::shared_ptr<IOnvifService> IOnvifServer::PTZService()
	{
		if (!ptz_service_)
		{
			ptz_service_ = OnvifServiceFactory()
				.Create(SERVICE_URI::PTZ, shared_from_this());
		}

		return ptz_service_;
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

	std::shared_ptr<IOnvifService> IOnvifServer::ReplayControlService()
	{
		if (!replay_control_service_)
		{
			replay_control_service_ = OnvifServiceFactory()
				.Create(SERVICE_URI::REPLAY, shared_from_this());
		}

		return replay_control_service_;
	}

	const std::shared_ptr<ILogger> IOnvifServer::Logger() const
	{
		return logger_;
	}

	std::shared_ptr<HttpServer> IOnvifServer::HttpServer() const
	{
		return http_server_;
	}

	std::shared_ptr<ServerConfigs> IOnvifServer::ServerConfigs()
	{
		return server_configs_;
	}

	const std::shared_ptr<pt::ptree>& IOnvifServer::ProfilesConfig() const
	{
		return profiles_config_;
	}

	std::string IOnvifServer::ServerAddress() const
	{
		std::string address{ "http://" };
		address += http_server_->config.address;
		address += ":" + std::to_string(server_configs_->enabled_http_port_forwarding ? server_configs_->forwarded_http_port
			: http_server_->config.port) + "/";

		return address;
	}
}