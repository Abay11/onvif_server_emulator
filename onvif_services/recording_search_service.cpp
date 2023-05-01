#include "recording_search_service.h"

#include "../Logger.h"

#include "../Server.h"

#include "../utility/AuthHelper.h"
#include "../onvif/OnvifRequest.h"


#include "../utility/SoapHelper.h"
#include "../utility/HttpHelper.h"
#include "../utility/DateTime.hpp"
#include "../utility/XmlParser.h"

#include "onvif_services/service_configs.h"

#include "../onvif/Recording.h"

#include <boost/property_tree/xml_parser.hpp>

namespace osrv
{
	namespace pt = boost::property_tree;

	// the list of implemented methods
	const std::string FindEvents{"FindEvents"};
	const std::string FindRecordings{"FindRecordings"};
	const std::string GetEventSearchResults{"GetEventSearchResults"};
	const std::string GetRecordingSearchResults{"GetRecordingSearchResults"};
	const std::string GetServiceCapabilities{"GetServiceCapabilities"};

	struct FindRecordingsHandler : public OnvifRequestBase
	{
		FindRecordingsHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs)
			: OnvifRequestBase(FindRecordings, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs) {}

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
	
	struct FindEventsHandler : public OnvifRequestBase
	{
		FindEventsHandler(std::shared_ptr<RecordingsMgr> rec_mgr, const std::map<std::string,
			std::string>& xs, const std::shared_ptr<pt::ptree>& configs)
			: OnvifRequestBase(FindEvents, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs)
		, rec_mgr_(rec_mgr) {}

		void operator()(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request) override
		{
			// TODO: more correct implementation should process requests parameters
			// Scope (IncludedSources, IncludedRecordings, RecordingInformationFilter, MaxMatches, KeepAliveTime

			// TODO: select required recording
			auto rec = rec_mgr_->Recordings().front();
			auto re = rec->RecordingEvents();

			auto ss = re->NewSearchSession(EventsSearchSessionFactory("SimpleEventsSearchSession"));

			auto envelope_tree = utility::soap::getEnvelopeTree(ns_);
			envelope_tree.add("s:Body.tse:FindEventsResponse.tse:SearchToken", ss->SearchToken());

			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", envelope_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);

			utility::http::fillResponseWithHeaders(*response, os.str());
		}

	private:
		std::shared_ptr<RecordingsMgr> rec_mgr_;
	};
	
	struct GetEventSearchResultsHandler : public OnvifRequestBase
	{
		GetEventSearchResultsHandler(std::shared_ptr<osrv::RecordingsMgr> rec_mgr,
			std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs)
			: OnvifRequestBase(GetEventSearchResults, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs)
			, rec_mgr_(rec_mgr)
		{}

		void operator()(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request) override
		{
			// TODO: more correct implementation should process requests parameters
			// MinResults, MaxResults, WaitTime

			pt::ptree request_xml;
			auto s = request->content.string();
			std::istringstream is(request->content.string());
			pt::xml_parser::read_xml(is, request_xml);
			auto searchToken = exns::find_hierarchy("Envelope.Body.GetEventSearchResults.SearchToken", request_xml);
			auto searchSession = rec_mgr_->Recordings().front()->RecordingEvents()->SearchSession(searchToken);

			pt::ptree results_tree;

			for (const auto& e : searchSession->Events())
			{
				pt::ptree ev_info;
				ev_info.add("tt:RecordingToken", e.recordingToken);
				ev_info.add("tt:TrackToken", e.trackToken);
				ev_info.add("tt:Time", utility::datetime::posix_datetime_to_utc(e.utcTime));
				ev_info.add("tt:Event.wsnt:Topic.<xmlattr>.Dialect", "http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet");
				ev_info.put("tt:Event.wsnt:Topic", "tns1:RecordingHistory/Track/State");
				ev_info.add("tt:Event.wsnt:Message.tt:Message.<xmlattr>.UtcTime", utility::datetime::posix_datetime_to_utc(e.utcTime));
				ev_info.add("tt:Event.wsnt:Message.tt:Message.<xmlattr>.PropertyOperation", "Changed");

				pt::ptree recordingItemDescr;
				recordingItemDescr.add("<xmlattr>.Name", "RecordingToken");
				recordingItemDescr.add("<xmlattr>.Value", e.recordingToken);
				ev_info.add_child("tt:Event.wsnt:Message.tt:Message.tt:Source.tt:SimpleItem", recordingItemDescr);

				pt::ptree trackItemDescr;
				trackItemDescr.add("<xmlattr>.Name", "Track");
				trackItemDescr.add("<xmlattr>.Value", e.trackToken);
				ev_info.add_child("tt:Event.wsnt:Message.tt:Message.tt:Source.tt:SimpleItem", trackItemDescr);

				ev_info.add("tt:Event.wsnt:Message.tt:Message.tt:Data.tt:SimpleItem.<xmlattr>.Name", "IsDataPresent");
				ev_info.add("tt:Event.wsnt:Message.tt:Message.tt:Data.tt:SimpleItem.<xmlattr>.Value", e.isDataPresent);
				ev_info.add("tt:StartStateEvent", false);

				results_tree.add_child("tt:Result", ev_info);
			}

			results_tree.add("tt:SearchState", "Completed");

			auto envelope_tree = utility::soap::getEnvelopeTree(ns_);
			envelope_tree.add_child("s:Body.tse:GetEventSearchResultsResponse.tse:ResultList", results_tree);

			pt::ptree root;
			root.add_child("s:Envelope", envelope_tree);
			std::ostringstream os;
			pt::write_xml(os, root);

			utility::http::fillResponseWithHeaders(*response, os.str());
		}

	private:
		std::shared_ptr<RecordingsMgr> rec_mgr_;
	};

	struct GetRecordingSearchResultsHandler : public OnvifRequestBase
	{
		GetRecordingSearchResultsHandler(std::shared_ptr<osrv::RecordingsMgr> rec_mgr,
			std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs)
			: OnvifRequestBase(GetRecordingSearchResults, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs)
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
				rec_info.add("tt:RecordingToken", r->Token());
				rec_info.add("tt:Source.tt:SourceId", "http://localhost/sourceID");
				rec_info.add("tt:Source.tt:Name", "DeviceName");
				rec_info.add("tt:Source.tt:Location", "Location description");
				rec_info.add("tt:Source.tt:Description", "Source description");
				rec_info.add("tt:Source.tt:Address", "http://localhost/address");
				
				rec_info.add("tt:EarliestRecording", utility::datetime::posix_datetime_to_utc(r->DateFrom()));
				rec_info.add("tt:LatestRecording", utility::datetime::posix_datetime_to_utc(r->DateUntil()));
				rec_info.add("tt:ContentRecording", "Content description");

				rec_info.add("tt:Track.tt:TrackToken", "VideoTrack_0");
				rec_info.add("tt:Track.tt:TrackType", "Video");
				rec_info.add("tt:Track.tt:Description", "Video track");
				rec_info.add("tt:Track.tt:DataFrom", utility::datetime::posix_datetime_to_utc(r->DateFrom()));
				rec_info.add("tt:Track.tt:DataTo", utility::datetime::posix_datetime_to_utc(r->DateUntil()));
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
		GetServiceCapabilitiesHandler(const std::map<std::string, std::string>& ns, const std::shared_ptr<pt::ptree>& configs)
			: OnvifRequestBase(GetServiceCapabilities,
			auth::SECURITY_LEVELS::PRE_AUTH, ns, configs)
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

		requestHandlers_.push_back(std::make_shared<FindEventsHandler>(rec_mgr_, xml_namespaces_, configs_ptree_));
		requestHandlers_.push_back(std::make_shared<FindRecordingsHandler>(xml_namespaces_, configs_ptree_));
		requestHandlers_.push_back(std::make_shared<GetEventSearchResultsHandler>(rec_mgr_, xml_namespaces_, configs_ptree_));
		requestHandlers_.push_back(std::make_shared<GetRecordingSearchResultsHandler>(rec_mgr_, xml_namespaces_, configs_ptree_));
		requestHandlers_.push_back(std::make_shared<GetServiceCapabilitiesHandler>(xml_namespaces_, configs_ptree_));
	}
}