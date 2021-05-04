#pragma once

#include "../HttpServerFwd.h"

#include "IOnvifService.h"

#include <string>

class ILogger;

namespace osrv
{
	//struct ServerConfigs;

	//namespace recording_search
	//{
		static const std::string NS = "https://www.onvif.org/ver10/search.wsdl";

	//	void init_service(HttpServer& /*srv*/, const osrv::ServerConfigs& /*configs*/,
	//		const std::string& /*configs_path*/, ILogger& /*logger*/);
	//}

	class RecordingSearchService : public IOnvifService
	{
	public:
		RecordingSearchService(const std::string& service_uri,
			const std::string& service_name, std::shared_ptr<IOnvifServer> srv);

		void handleRequestImpl(std::shared_ptr<std::ostream> response,
			const std::istream& request) override;

	private:

	};
}
