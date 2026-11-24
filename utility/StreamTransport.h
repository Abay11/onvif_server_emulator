#pragma once
#include <boost/property_tree/ptree.hpp>
#include <string>
namespace osrv
{
struct ServerConfigs;
const boost::property_tree::ptree& StreamOperation(const boost::property_tree::ptree& document,
																									 const std::string& operation);
// Returns the advertised base URI for the requested ONVIF transport.
std::string StreamAddress(const ServerConfigs& server, const boost::property_tree::ptree& operation, bool media2,
													bool secure);
} // namespace osrv
