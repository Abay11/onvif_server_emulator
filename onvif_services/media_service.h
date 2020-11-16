#pragma once

#include "../Types.inl"

#include <boost/property_tree/ptree_fwd.hpp>

class Logger;

namespace osrv
{
	namespace media
	{
		void init_service(HttpServer& srv, const std::string& configs_path, Logger& logger);

		namespace util
		{
			namespace pt = boost::property_tree;
			void fill_soap_videosource_configuration(const pt::ptree& config_node, pt::ptree& videosource_node);
		}
	}
}