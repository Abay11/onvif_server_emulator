#pragma once

#include "../HttpServerFwd.h"

#include <boost/property_tree/ptree_fwd.hpp>

#include <string>
#include <memory>
#include <stdexcept>

class ILogger;

namespace osrv
{
	class IOnvifServer;
	class ServerConfigs;

	namespace pt = boost::property_tree;

	class IOnvifService
	{
	public:
		IOnvifService(const std::string& service_name,
			std::shared_ptr<IOnvifServer> srv);

		virtual ~IOnvifService() {}

		void Run();

	protected:
		//virtual void runImpl() = 0;

		virtual void handleRequestImpl() = 0;

	protected:
		const std::string service_name_;
		std::shared_ptr<pt::ptree> configs_ptree_;
		std::shared_ptr<IOnvifServer> onvif_server_;
		std::shared_ptr<HttpServer> http_server_;
		std::shared_ptr<osrv::ServerConfigs> server_configs_;
		const std::shared_ptr<ILogger> log_;
	};

}