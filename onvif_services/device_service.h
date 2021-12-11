#pragma once

#include "IOnvifService.h"

#include "../HttpServerFwd.h"

#include <boost/property_tree/ptree_fwd.hpp>

class ILogger;


namespace osrv
{
	struct ServerConfigs;

	//namespace device
	//{
	//	extern const std::string CONFIGS_FILE;

	//	void init_service(HttpServer& srv, osrv::ServerConfigs& /*configs*/,
	//		const std::string& /*configs_path*/, ILogger& /*logger*/);

	//	// NOTE: this may not be safe, if this service was not initialized before invocation
	//	const boost::property_tree::ptree& get_configs_tree_instance();

	//}

	class DeviceService : public IOnvifService
	{
	public:
		DeviceService(const std::string& service_uri,
			const std::string& service_name, std::shared_ptr<IOnvifServer> srv);

	private:
	};

}