#pragma once
#include <boost/property_tree/ptree_fwd.hpp>
#include <map>
#include <memory>
#include <string>
#include <vector>
namespace osrv
{
class IOnvifServer;
struct OnvifRequestBase;
void AddMetadataHandlers(std::vector<std::shared_ptr<OnvifRequestBase>>& handlers,
												 const std::map<std::string, std::string>& namespaces,
												 const std::shared_ptr<boost::property_tree::ptree>& configuration,
												 std::shared_ptr<IOnvifServer> server, bool media2);
} // namespace osrv
