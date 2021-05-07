#include "recording_search_service.h"

#include "../Logger.h"

#include "../Server.h"

#include "../utility/AuthHelper.h"
#include "../onvif/OnvifRequest.h"

namespace osrv
{
	namespace pt = boost::property_tree;

	// the list of implemented methods
	const std::string GetServiceCapabilities{"GetServiceCapabilities"};

	struct GetServiceCapabilitiesHandler : public OnvifRequestBase
	{
		GetServiceCapabilitiesHandler() : OnvifRequestBase(GetServiceCapabilities,
			auth::SECURITY_LEVELS::PRE_AUTH)
		{
		}

		void operator()(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request) override
		{
			throw std::runtime_error("GetServiceCapabilities is still implementing");
		}

	};

	RecordingSearchService::RecordingSearchService(const std::string& service_uri,
		const std::string& service_name, std::shared_ptr<IOnvifServer> srv)
		: IOnvifService(service_uri, service_name, srv)
	{
		requestHandlers_.push_back(std::make_shared<GetServiceCapabilitiesHandler>());
	}
}