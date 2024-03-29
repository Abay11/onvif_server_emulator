#pragma once

#include "../HttpServerFwd.h"
//#include <../Simple-Web-Server/server_http.hpp>

#include "../utility/AuthHelper.h"

#include <boost/property_tree/ptree_fwd.hpp>

#include <istream>
#include <map>
#include <memory>
#include <ostream>
#include <string>

class ILogger;

namespace osrv
{
class IOnvifServer;
class ServerConfigs;

struct OnvifRequestBase;
using HandlerSP = std::shared_ptr<OnvifRequestBase>;

namespace pt = boost::property_tree;

class IOnvifService : public std::enable_shared_from_this<IOnvifService>
{
public:
	IOnvifService(const std::string& service_uri, const std::string& service_name, std::shared_ptr<IOnvifServer> srv);

	virtual ~IOnvifService()
	{
	}

	void Run();

	std::string ServiceName()
	{
		return service_name_;
	}

	std::shared_ptr<IOnvifServer> OnvifServer()
	{
		return onvif_server_;
	}

	const std::vector<HandlerSP>& Handlers() const
	{
		return requestHandlers_;
	}

	const std::shared_ptr<pt::ptree> Configs() const
	{
		return configs_ptree_;
	}

protected:
	const std::string service_uri_;
	const std::string service_name_;
	std::shared_ptr<pt::ptree> configs_ptree_;
	std::shared_ptr<IOnvifServer> onvif_server_;
	std::shared_ptr<HttpServer> http_server_;
	std::shared_ptr<osrv::ServerConfigs> server_configs_;
	const std::shared_ptr<ILogger> log_;

	std::map<std::string, std::string> xml_namespaces_;

	std::vector<HandlerSP> requestHandlers_;

private:
	bool is_running_ = false;
};

} // namespace osrv