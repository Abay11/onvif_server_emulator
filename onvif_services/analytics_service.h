#pragma once

#include "IOnvifService.h"

#include "../HttpServerFwd.h"

#include <boost/property_tree/ptree_fwd.hpp>

#include <string>

class ILogger;

namespace osrv
{

namespace analytics
{

void fillModules(const boost::property_tree::ptree& modules, boost::property_tree::ptree& out, std::string_view xns);

}

class AnalyticsService : public IOnvifService
{
public:
	AnalyticsService(const std::string& service_uri, const std::string& service_name, std::shared_ptr<IOnvifServer> srv);
};

} // namespace osrv