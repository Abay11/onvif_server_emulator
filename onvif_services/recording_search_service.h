#pragma once

#include "../HttpServerFwd.h"

#include "IOnvifService.h"

#include <string>

class ILogger;

namespace osrv
{
	//static const std::string NS = "https://www.onvif.org/ver10/search.wsdl";

	class RecordingSearchService : public IOnvifService
	{
	public:
		RecordingSearchService(const std::string& service_uri,
			const std::string& service_name, std::shared_ptr<IOnvifServer> srv);

	private:
	};
}
