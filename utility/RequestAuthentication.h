#pragma once
#include "HttpHelper.h"
#include <boost/property_tree/ptree.hpp>
#include <iosfwd>
namespace osrv
{
struct ServerConfigs;
}
namespace utility::auth
{
struct Rejected : osrv::auth::digest_failed
{
	bool soap;
	bool stale;
	Rejected(bool soapFault, bool expired = false) : soap(soapFault), stale(expired)
	{
	}
};
// Call only with a verified identity. Denial is a SOAP NotAuthorized fault, never a Digest challenge.
void Authorize(osrv::auth::USER_TYPE user, osrv::auth::SECURITY_LEVELS level);
osrv::auth::USER_TYPE Authenticate(const osrv::ServerConfigs& config, const osrv::HttpServer::Request& request,
																	 const boost::property_tree::ptree& soap, osrv::auth::SECURITY_LEVELS level);
void Reject(std::ostream& response, const osrv::ServerConfigs& config, const Rejected& failure);
} // namespace utility::auth
