#include "..\..\include\onvif_services\service_configs.h"

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

namespace
{
	namespace pt = boost::property_tree;

}

namespace osrv
{
	ServiceConfigs::operator const PTreeSP()
	{
		auto json_config_ = std::make_shared<pt::ptree>();
		pt::read_json(ConfigPath(config_path_, ConfigName(service_name_)), *json_config_);
		return json_config_;
	}
}
