#pragma once

#include "../Types.inl"

class ILogger;

#include <string>

#include <boost/property_tree/ptree_fwd.hpp>

namespace osrv
{
	struct ServerConfigs;

	namespace media2
	{
		void init_service(HttpServer& srv, const ServerConfigs& server_configs_ptr,
			const std::string& configs_path, ILogger& logger);

		namespace util
		{
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