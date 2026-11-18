#pragma once

#include "IOnvifService.h"

#include "../HttpServerFwd.h"

#include <boost/property_tree/ptree_fwd.hpp>

#include <string>

class ILogger;

namespace utility::media
{
class MediaProfilesManager;
}

namespace osrv
{

namespace analytics
{

void fillConfiguration(const boost::property_tree::ptree& json, boost::property_tree::ptree& xml,
											 const utility::media::MediaProfilesManager& manager);

void fillModules(const boost::property_tree::ptree& modules, boost::property_tree::ptree& out, std::string_view xns);

} // namespace analytics

class AnalyticsService : public IOnvifService
{
public:
	AnalyticsService(const std::string& service_uri, const std::string& service_name, std::shared_ptr<IOnvifServer> srv);
};

} // namespace osrv