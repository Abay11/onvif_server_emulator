#include "deviceio_service.h"

#include "IOnvifServer.h"

#include "../onvif/OnvifRequest.h"
#include "../utility/ChannelTopology.h"
#include "../utility/MediaProfilesManager.h"
#include "../utility/HttpHelper.h"
#include "../utility/SoapHelper.h"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

// List of implemented methods
const std::string GetVideoSources{"GetVideoSources"};
const std::string GetAudioSources{"GetAudioSources"};

namespace pt = boost::property_tree;

namespace osrv
{
struct GetInputsHandler : public OnvifRequestBase
{
private:
	const std::shared_ptr<IOnvifServer> onvif_srv_;

public:
	GetInputsHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& serviceConfigs,
												 const std::shared_ptr<IOnvifServer>& srv, const std::string& operation)
			: OnvifRequestBase(operation, auth::SECURITY_LEVELS::READ_MEDIA, xs, serviceConfigs), onvif_srv_(srv)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		auto envelope_tree = utility::soap::getEnvelopeTree(ns_);
		pt::ptree response_node;

		const auto& manager = *onvif_srv_->MediaProfilesManager();
		if (name() == GetAudioSources)
		{
			for (const auto& [_, audio] : manager.ReaderWriter()->ConfigsTree().get_child("AudioSources"))
				response_node.add("tmd:Token", audio.get<std::string>("Token"));
		}
		else if (const auto* topology = manager.Topology())
		{
			for (const auto& [_, channel] : topology->Channels())
				response_node.add("tmd:Token", channel.get<std::string>("videoSource.token"));
		}
		else
		{
			auto media_config = onvif_srv_->MediaService()->Configs();
			for (const auto& [_, video] : media_config->get_child("GetVideoSources"))
				response_node.add("tmd:Token", video.get<std::string>("token"));
		}
		envelope_tree.put_child("s:Body.tmd:" + name() + "Response", response_node);

		pt::ptree root_tree;
		root_tree.put_child("s:Envelope", envelope_tree);

		std::ostringstream os;
		pt::write_xml(os, root_tree);

		utility::http::fillResponseWithHeaders(*response, os.str());
	}
};

DeviceIOService::DeviceIOService(const std::string& service_uri, const std::string& service_name,
																 std::shared_ptr<IOnvifServer> srv)
		: IOnvifService(service_uri, service_name, srv)
{
	requestHandlers_.push_back(std::make_shared<GetInputsHandler>(xml_namespaces_, configs_ptree_, srv, GetVideoSources));
	requestHandlers_.push_back(std::make_shared<GetInputsHandler>(xml_namespaces_, configs_ptree_, srv, GetAudioSources));
}

} // namespace osrv
