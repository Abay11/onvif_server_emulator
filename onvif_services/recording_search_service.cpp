#include "recording_search_service.h"

#include "../Logger.h"

#include "../Server.h"

#include "../utility/AuthHelper.h"
#include "../onvif/OnvifRequest.h"

#include "../utility/SoapHelper.h"
#include "../utility/HttpHelper.h"

#include <boost/property_tree/xml_parser.hpp>

namespace osrv
{
	namespace pt = boost::property_tree;

	// the list of implemented methods
	const std::string FindRecordings{"FindRecordings"};
	const std::string GetRecordingSearchResults{"GetRecordingSearchResults"};
	const std::string GetServiceCapabilities{"GetServiceCapabilities"};

	struct FindRecordingsHandler : public OnvifRequestBase
	{
		FindRecordingsHandler(const std::map<std::string, std::string>& xs)
			: OnvifRequestBase(FindRecordings, auth::SECURITY_LEVELS::READ_MEDIA, xs) {}

		void operator()(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request) override
		{
			// TODO: more correct implementation should process requests parameters

			auto envelope_tree = utility::soap::getEnvelopeTree(ns_);
			envelope_tree.add("s:Body.tse:FindRecordingsResponse.tse:SearchToken",
				std::to_string(searchToken_++));

			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", envelope_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);

			utility::http::fillResponseWithHeaders(*response, os.str());
		}

	private:
		unsigned int searchToken_ = 0;
	};

	struct GetServiceCapabilitiesHandler : public OnvifRequestBase
	{
		GetServiceCapabilitiesHandler(const std::map<std::string, std::string>& ns) : OnvifRequestBase(GetServiceCapabilities,
			auth::SECURITY_LEVELS::PRE_AUTH, ns)
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
		requestHandlers_.push_back(std::make_shared<FindRecordingsHandler>(xml_namespaces_));
		requestHandlers_.push_back(std::make_shared<GetServiceCapabilitiesHandler>(xml_namespaces_));
	}
}