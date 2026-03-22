
#include "replay_control_service.h"

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
									 const std::shared_ptr<pt::ptree>& configs)
			: OnvifRequestBase(GetReplayUri, auth::SECURITY_LEVELS::READ_MEDIA, ns, configs), srv_configs_(srv_configs)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		auto serverIPv4Addr = srv_configs_->ipv4_address_;

		if (auto ep = request->local_endpoint(); ep != decltype(ep){})
		{
			auto ip = ep.address().to_v4().to_string();
			if (!ip.empty())
				serverIPv4Addr = ip;
		}

		// TODO: uri now is hard coded
		const auto uri = std::format("rtsp://{}:{}/Recording0", serverIPv4Addr, srv_configs_->rtsp_port_);

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
};

ReplayControlService::ReplayControlService(const std::string& service_uri, const std::string& service_name,
																					 std::shared_ptr<IOnvifServer> srv)
		: IOnvifService(service_uri, service_name, srv)

{
	requestHandlers_.push_back(std::make_shared<GetReplayHandler>(xml_namespaces_, srv->GetServerConfigs(), configs_ptree_));
}
} // namespace osrv
