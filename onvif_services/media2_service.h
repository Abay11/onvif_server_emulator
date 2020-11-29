#pragma once

#include "../Types.inl"

class Logger;

#include <string>

#include <boost/property_tree/ptree_fwd.hpp>

namespace osrv
{
	struct ServerConfigs;

	namespace media2
	{
		void init_service(HttpServer& srv, const ServerConfigs& server_configs_ptr,
			const std::string& configs_path, Logger& logger);

		namespace util
		{
			using ptree = boost::property_tree::ptree;
			// functions throw an exception if error occured
			void profile_to_soap(const ptree& profile_config, const ptree& configs_file, ptree& result);
			void fill_video_encoder(const ptree& config_node, ptree& videoencoder_node);
		}
	}
}