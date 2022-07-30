#include "deviceio_service.h"

#include "IOnvifServer.h"

#include "../onvif/OnvifRequest.h"
#include "../utility/HttpHelper.h"
#include "../utility/SoapHelper.h"

#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

//List of implemented methods
const std::string GetVideoSources{ "GetVideoSources" };

namespace pt = boost::property_tree;

namespace osrv
{
	struct GetVideoSourcesHandler : public OnvifRequestBase
	{
	private:
		const std::shared_ptr<IOnvifServer> onvif_srv_;
		
	public:
		GetVideoSourcesHandler(const std::map<std::string, std::string>& xs,
			const std::shared_ptr<pt::ptree>& serviceConfigs,
			const std::shared_ptr<IOnvifServer>& srv)
			: OnvifRequestBase(GetVideoSources, auth::SECURITY_LEVELS::READ_MEDIA, xs, serviceConfigs)
			, onvif_srv_(srv)
		{
		}

		void operator()(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request) override
		{
			auto envelope_tree = utility::soap::getEnvelopeTree(ns_);
			pt::ptree response_node;

			// currently all videosources described in the Media service's config file
			auto media_service_configs = onvif_srv_->MediaService()->Configs();
			const auto& videoSources = media_service_configs->get_child("GetVideoSources");
			for (const auto& [name, config_tree] : videoSources)
			{
				response_node.add("tmd:GetVideoSourcesResponse.tmd:Token",
					config_tree.get<std::string>("token"));
			}

			envelope_tree.add_child("s:Body", response_node);

			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", envelope_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);

			utility::http::fillResponseWithHeaders(*response, os.str());
		}
	};

DeviceIOService::DeviceIOService(const std::string& service_uri, const std::string& service_name, std::shared_ptr<IOnvifServer> srv)
	: IOnvifService(service_uri, service_name, srv)
{
	requestHandlers_.push_back(std::make_shared<GetVideoSourcesHandler>(xml_namespaces_, configs_ptree_, srv));
}

} // osrv
