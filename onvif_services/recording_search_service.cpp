#include "recording_search_service.h"

#include "../Logger.h"

#include "../Server.h"

#include "../utility/AuthHelper.h"
#include "../onvif/OnvifRequest.h"


#include "../utility/SoapHelper.h"
#include "../utility/HttpHelper.h"
#include "../utility/DateTime.hpp"

#include "onvif_services/service_configs.h"

#include "../onvif/Recording.h"

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
			// Scope (IncludedSources, IncludedRecordings, RecordingInformationFilter, MaxMatches, KeepAliveTime

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
	
	struct GetRecordingSearchResultsHandler : public OnvifRequestBase
	{
		GetRecordingSearchResultsHandler(std::shared_ptr<osrv::RecordingsMgr> rec_mgr,
			std::map<std::string, std::string>& xs)
			: OnvifRequestBase(GetRecordingSearchResults, auth::SECURITY_LEVELS::READ_MEDIA, xs)
			, rec_mgr_(rec_mgr)
		{}

		void operator()(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request) override
		{
			// TODO: more correct implementation should process requests parameters
			// MinResults, MaxResults, WaitTime

			pt::ptree results_tree;

			for (const auto& r : rec_mgr_->Recordings())
			{
				pt::ptree rec_info;
				rec_info.add("tt:RecordingToken", r.Name());
				rec_info.add("tt:Source.tt:SourceId", "http://localhost/sourceID");
				rec_info.add("tt:Source.tt:Name", "DeviceName");
				rec_info.add("tt:Source.tt:Location", "Location description");
				rec_info.add("tt:Source.tt:Description", "Source description");
				rec_info.add("tt:Source.tt:Address", "http://localhost/address");
				
				rec_info.add("tt:EarliestRecording", utility::datetime::posix_datetime_to_utc(r.DateFrom()));
				rec_info.add("tt:LatestRecording", utility::datetime::posix_datetime_to_utc(r.DateUntil()));
				rec_info.add("tt:ContentRecording", "Content description");

				rec_info.add("tt:Track.tt:TrackToken", "VideoTrack_0");
				rec_info.add("tt:Track.tt:TrackType", "Video");
				rec_info.add("tt:Track.tt:Description", "Video track");
				rec_info.add("tt:Track.tt:DataFrom", utility::datetime::posix_datetime_to_utc(r.DateFrom()));
				rec_info.add("tt:Track.tt:DataTo", utility::datetime::posix_datetime_to_utc(r.DateUntil()));
				rec_info.add("tt:Track.tt:RecordingStatus", "Initiated");

				results_tree.add_child("tt:RecordingInformation", rec_info);
			}

			results_tree.add("tt:SearchState", "Completed");

			auto envelope_tree = utility::soap::getEnvelopeTree(ns_);
			envelope_tree.add_child("s:Body.tse:GetRecordingSearchResultsResponse.tse:ResultList", results_tree);

			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", envelope_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);

			utility::http::fillResponseWithHeaders(*response, os.str());
		}

	private:
		std::shared_ptr<RecordingsMgr> rec_mgr_;
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
		rec_mgr_ = std::make_shared<RecordingsMgr>(ConfigPath(srv->ConfigsPath(), ConfigName(service_name)));

		requestHandlers_.push_back(std::make_shared<FindRecordingsHandler>(xml_namespaces_));
		requestHandlers_.push_back(
			std::make_shared<GetRecordingSearchResultsHandler>(rec_mgr_, xml_namespaces_));
		requestHandlers_.push_back(std::make_shared<GetServiceCapabilitiesHandler>(xml_namespaces_));
	}
}