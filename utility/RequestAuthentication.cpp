#include "RequestAuthentication.h"
#include "../Server.h"
namespace utility::auth
{
void Authorize(osrv::auth::USER_TYPE user, osrv::auth::SECURITY_LEVELS level)
{
	if (!osrv::auth::isUserHasAccess(user, level))
		throw Rejected(true);
}

osrv::auth::USER_TYPE Authenticate(const osrv::ServerConfigs& config, const osrv::HttpServer::Request& request,
																	 const boost::property_tree::ptree& soap, osrv::auth::SECURITY_LEVELS level)
{
	using namespace osrv;
	if (config.auth_scheme_ == AUTH_SCHEME::NONE)
		return osrv::auth::USER_TYPE::ANON;
	if (level == osrv::auth::SECURITY_LEVELS::PRE_AUTH)
		return osrv::auth::USER_TYPE::ANON;
	const bool digest = config.auth_scheme_ == AUTH_SCHEME::DIGEST || config.auth_scheme_ == AUTH_SCHEME::DIGEST_WSS;
	const bool wss = config.auth_scheme_ == AUTH_SCHEME::WSS || config.auth_scheme_ == AUTH_SCHEME::DIGEST_WSS;
	auto authorization = request.header.find("Authorization");
	std::string username;
	bool stale = false;
	bool soapFailure = !digest;
	if (digest && authorization != request.header.end())
	{
		auto credentials = utility::digest::extract_DA(authorization->second);
		credentials.method = request.method;
		credentials.target = request.path + (request.query_string.empty() ? "" : "?" + request.query_string);
		if (request.header.count("Authorization") == 1 && config.digest_session_->verifyDigest(credentials, stale))
			username = credentials.username;
	}
	if (digest && authorization != request.header.end() && username.empty())
		throw Rejected(false, stale);
	if (wss && (utility::digest::hasSecurityHeader(soap) || !digest))
	{
		soapFailure = true;
		auto wsUsername = config.digest_session_->verifyUsernameToken(soap);
		if (wsUsername.empty() || (!username.empty() && wsUsername != username))
			throw Rejected(true);
		username = wsUsername;
	}
	auto type = osrv::auth::get_usertype_by_username(username, config.digest_session_->get_users_list());
	if (username.empty())
		throw Rejected(soapFailure, stale);
	Authorize(type, level);
	return type;
}
void Reject(std::ostream& response, const osrv::ServerConfigs& config, const Rejected& failure)
{
	if (failure.soap)
	{
		const std::string body =
				"<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" "
				"xmlns:ter=\"http://www.onvif.org/ver10/error\">"
				"<s:Body><s:Fault><s:Code><s:Value>s:Sender</s:Value><s:Subcode><s:Value>ter:NotAuthorized</s:Value>"
				"</s:Subcode></s:Code><s:Reason><s:Text xml:lang=\"en\">Sender not Authorized</s:Text></s:Reason>"
				"</s:Fault></s:Body></s:Envelope>";
		response << "HTTP/1.1 400 Bad Request\r\nContent-Type: application/soap+xml; charset=utf-8\r\nContent-Length: "
						 << body.size() << "\r\n\r\n"
						 << body;
	}
	else
		response << "HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\n"
						 << config.digest_session_->challenges(failure.stale) << "\r\n";
}
} // namespace utility::auth
