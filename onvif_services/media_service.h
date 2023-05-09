#pragma once

#include "IOnvifService.h"

#include "../HttpServerFwd.h"

#include <boost/property_tree/ptree_fwd.hpp>

class ILogger;

namespace osrv
{
class MediaService : public IOnvifService
{
public:
	MediaService(const std::string& service_uri, const std::string& service_name, std::shared_ptr<IOnvifServer> srv);

private:
};

struct ServerConfigs;

namespace media
{
namespace util
{
std::string generate_rtsp_url(const ServerConfigs& server_configs, const std::string& profile_stream_url);

namespace pt = boost::property_tree;
void fill_soap_videosource_configuration(const pt::ptree& config_node, pt::ptree& videosource_node);

void fill_analytics_configuration(/*const pt::ptree& config_node,*/ pt::ptree& /*result*/);

void fillAEConfig(const pt::ptree& /*in*/, pt::ptree& /*out*/);

class MultichannelProfilesNamesConverter
{
public:
	MultichannelProfilesNamesConverter(const std::string& name) : name_(name)
	{
	}

	std::string CleanedName()
	{
		auto start_pos = name_.find('_') + 1;
		return name_.substr(start_pos);
	}

private:
	const std::string& name_;
};
} // namespace util
} // namespace media
} // namespace osrv