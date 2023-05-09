#pragma once

#include "IOnvifService.h"

#include "../HttpServerFwd.h"

#include <boost/property_tree/ptree_fwd.hpp>

#include <string>

class ILogger;

namespace osrv
{
struct ServerConfigs;

class Media2Service : public IOnvifService
{
public:
	Media2Service(const std::string& service_uri, const std::string& service_name, std::shared_ptr<IOnvifServer> srv);
};

namespace media2
{
namespace util
{
// TODO: The same function is used in media. Replace it with one
std::string generate_rtsp_url(const ServerConfigs& server_configs, const std::string& profile_stream_url);

using ptree = boost::property_tree::ptree;
// functions throw an exception if error occured
void profile_to_soap(const ptree& profile_config, const ptree& configs_file, ptree& result);
void fill_video_encoder(const ptree& config_node, ptree& videoencoder_node);

template <typename T> std::string to_value_list(const std::vector<T>& list)
{
	std::stringstream ss;
	for (const auto& t : list)
	{
		if (!ss.str().empty())
		{
			ss << " ";
		}
		ss << t;
	}

	return ss.str();
}
} // namespace util
} // namespace media2
} // namespace osrv