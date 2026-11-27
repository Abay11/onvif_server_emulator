#include "replay_control_service.h"
#include "../onvif/Recording.h"
#include "../utility/MetadataConfiguration.h"
#include "../utility/StreamTransport.h"
#include "../utility/XmlParser.h"
#include <set>

#include "../onvif/OnvifRequest.h"

#include "../utility/HttpHelper.h"
#include "../utility/SoapHelper.h"

// TODO: it was included only for made ServerConfigs declaration
// move ServerConfigs to a separate file
#include "../Server.h"

#include <boost/property_tree/xml_parser.hpp>

// the list of implemented methods
const std::string GetReplayUri{"GetReplayUri"};

namespace pt = boost::property_tree;

namespace osrv
{
struct GetReplayHandler : public OnvifRequestBase
{
	GetReplayHandler(const std::map<std::string, std::string>& ns, const std::shared_ptr<osrv::ServerConfigs> srv_configs,
									 const std::shared_ptr<pt::ptree>& configs, const std::string& configs_path)
			: OnvifRequestBase(GetReplayUri, auth::SECURITY_LEVELS::READ_MEDIA, ns, configs), srv_configs_(srv_configs)
	{
		if (!srv_configs_->archive)
			for (const auto& recording : RecordingsReaderFromConfig(configs_path + "/recording_search.config").Recordings())
				recordings_.insert(recording->Token());
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		pt::ptree document;
		std::istringstream input(request->content.string());
		pt::read_xml(input, document);
		const auto& operation = StreamOperation(document, "GetReplayUri");
		auto recording = exns::find_hierarchy("Envelope.Body.GetReplayUri.RecordingToken", document);
		const auto base = StreamAddress(*srv_configs_, operation, false, HttpServer::IsSecure(request));
		std::string uri;
		if (srv_configs_->archive)
		{
			if (!srv_configs_->archive->Recording(recording))
				throw metadata::Fault("NoRecording", "The recording does not exist");
			srv_configs_->prepare_replay(recording);
			uri = base + "onvif/replay/" + recording;
		}
		else
		{
			if (!recordings_.contains(recording))
				throw metadata::Fault("NoRecording", "The recording does not exist");
			uri = base + "Recording0";
		}

		auto envelope_tree = utility::soap::getEnvelopeTree(ns_);
		envelope_tree.add("s:Body.trp:GetReplayUriResponse.trp:Uri", uri);

		pt::ptree root_tree;
		root_tree.put_child("s:Envelope", envelope_tree);

		std::ostringstream os;
		pt::write_xml(os, root_tree);

		utility::http::fillResponseWithHeaders(*response, os.str());
	}

private:
	const std::shared_ptr<osrv::ServerConfigs> srv_configs_;
	std::set<std::string> recordings_;
};

ReplayControlService::ReplayControlService(const std::string& service_uri, const std::string& service_name,
																					 std::shared_ptr<IOnvifServer> srv)
		: IOnvifService(service_uri, service_name, srv)

{
	requestHandlers_.push_back(std::make_shared<GetReplayHandler>(
			xml_namespaces_, srv->GetServerConfigs(), configs_ptree_, srv->ConfigsPath()));
}
} // namespace osrv
