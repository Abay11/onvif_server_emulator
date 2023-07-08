#pragma once

#include "../HttpServerFwd.h"

#include <boost/property_tree/ptree_fwd.hpp>

#include <memory>
#include <string>
#include <vector>

namespace pt = boost::property_tree;

class ILogger;

namespace utility::media
{
class MediaProfilesManager;
}

namespace osrv
{
class IOnvifService;
class IOnvifServer;
class ServerConfigs;

namespace SERVICE_URI
{
const std::string ANALYTICS = "http://www.onvif.org/ver20/analytics/wsdl";
const std::string DEVICE = "http://www.onvif.org/ver10/device/wsdl";
const std::string DEVICEIO = "http://www.onvif.org/ver10/deviceIO/wsdl";
const std::string IMAGING = "http://www.onvif.org/ver20/imaging/wsdl";
const std::string MEDIA = "http://www.onvif.org/ver10/media/wsdl";
const std::string MEDIA2 = "http://www.onvif.org/ver20/media/wsdl";
const std::string PTZ = "http://www.onvif.org/ver20/ptz/wsdl";
const std::string REPLAY = "http://www.onvif.org/ver10/replay/wsdl";
const std::string SEARCH = "http://www.onvif.org/ver10/search/wsdl";
}; // namespace SERVICE_URI

class OnvifServiceFactory
{
public:
	std::shared_ptr<IOnvifService> Create(const std::string& service_uri, std::shared_ptr<IOnvifServer> srv);
};

class IOnvifServer : public std::enable_shared_from_this<IOnvifServer>
{
public:
	IOnvifServer(const std::string& configs_path, std::shared_ptr<ILogger> logger);

	const std::string& ConfigsPath() const;

	std::shared_ptr<IOnvifService> AnalyticsService();
	std::shared_ptr<IOnvifService> DeviceService();
	std::shared_ptr<IOnvifService> DeviceIOService();
	std::shared_ptr<IOnvifService> ImagingService();
	std::shared_ptr<IOnvifService> MediaService();
	std::shared_ptr<IOnvifService> Media2Service();
	std::shared_ptr<IOnvifService> PTZService();
	std::shared_ptr<IOnvifService> RecordingSearchService();
	std::shared_ptr<IOnvifService> ReplayControlService();

	const std::shared_ptr<ILogger> Logger() const;
	std::shared_ptr<HttpServer> HttpServer() const;
	std::shared_ptr<ServerConfigs> ServerConfigs();
	const std::shared_ptr<pt::ptree>& ProfilesConfig() const;

	std::string ServerAddress() const;

	utility::media::MediaProfilesManager* MediaProfilesManager()
	{
		return media_profiles_manager_.get();
	};

protected:
	const std::string& configs_path_;

	std::shared_ptr<ILogger> logger_;

	std::shared_ptr<osrv::ServerConfigs> server_configs_;
	std::shared_ptr<pt::ptree> profiles_config_;

	std::shared_ptr<osrv::HttpServer> http_server_;

private:
	std::shared_ptr<IOnvifService> analytics_service_;
	std::shared_ptr<IOnvifService> device_service_;
	std::shared_ptr<IOnvifService> deviceio_service_;
	std::shared_ptr<IOnvifService> imaging_service_;
	std::shared_ptr<IOnvifService> media_service_;
	std::shared_ptr<IOnvifService> media2_service_;
	std::shared_ptr<IOnvifService> ptz_service_;
	std::shared_ptr<IOnvifService> recording_search_service_;
	std::shared_ptr<IOnvifService> replay_control_service_;

	std::shared_ptr<utility::media::MediaProfilesManager> media_profiles_manager_;
};
} // namespace osrv