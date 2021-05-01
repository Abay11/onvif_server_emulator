
#pragma once

#include "../HttpServerFwd.h"

#include <vector>
#include <memory>
#include <string>

class ILogger;

namespace osrv
{
	class IOnvifService;
	class IOnvifServer;
	class ServerConfigs;

	namespace SERVICE_URI
	{
		const std::string DEVICE = "https://www.onvif.org/ver10/device/wsdl/devicemgmt.wsdl";
		const std::string SEARCH = "https://www.onvif.org/ver10/search.wsdl";
	};

	class OnvifServiceFactory
	{
	public:
		std::shared_ptr<IOnvifService> Create(const std::string& service_uri,
			std::shared_ptr<IOnvifServer> srv);
	};

	class IOnvifServer : public std::enable_shared_from_this<IOnvifServer>
	{
	public:
		IOnvifServer(const std::string& configs_path)
			: configs_path_(configs_path)
		{}

		const std::string& ConfigsPath() const;

		std::shared_ptr<IOnvifService> DeviceService();
		std::shared_ptr<IOnvifService> RecordingSearchService();

		const std::shared_ptr<ILogger> Logger() const;
		std::shared_ptr<HttpServer> HttpServer();
		std::shared_ptr<ServerConfigs> ServerConfigs();

	protected:
		std::shared_ptr<ILogger> logger_;

		const std::string& configs_path_;
		
		std::shared_ptr<osrv::ServerConfigs> server_configs_;

		std::shared_ptr<osrv::HttpServer> http_server_;

		std::shared_ptr<IOnvifService> recording_search_service_;
	};
}