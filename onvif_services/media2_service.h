#pragma once

#include "../HttpServerFwd.h"

class ILogger;

#include <string>

#include <boost/property_tree/ptree_fwd.hpp>

namespace osrv
{
	struct ServerConfigs;

	namespace media2

	{
		const boost::property_tree::ptree& config_instance();
		
		void init_service(HttpServer& srv, const ServerConfigs& server_configs_ptr,
			const std::string& configs_path, ILogger& logger);

		namespace util
		{
			// TODO: The same function is used in media. Replace it with one
			std::string generate_rtsp_url(const ServerConfigs& server_configs, const std::string& profile_stream_url);

			using ptree = boost::property_tree::ptree;
			// functions throw an exception if error occured
			void profile_to_soap(const ptree& profile_config, const ptree& configs_file, ptree& result);
			void fill_video_encoder(const ptree& config_node, ptree& videoencoder_node);

			template <typename T>
			std::string to_value_list(const std::vector<T>& list)
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
		}
	}
}