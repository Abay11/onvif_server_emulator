#include "analytics_service.h"

#include "../Logger.h"
#include "../Server.h"

#include "../onvif/OnvifRequest.h"
#include "../onvif_services/device_service.h"
#include "../utility/MediaProfilesManager.h"

#include "../utility/HttpHelper.h"
#include "../utility/SoapHelper.h"
#include "../utility/XmlParser.h"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

namespace pt = boost::property_tree;

// a list of implemented methods
const std::string GetServiceCapabilities = "GetServiceCapabilities";

namespace osrv
{

namespace analytics
{

struct GetServiceCapabilitiesHandler : public OnvifRequestBase
{
private:
	const pt::ptree& m_device_service_configs;

public:
	GetServiceCapabilitiesHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs,
																const pt::ptree& device_service_configs)
			: OnvifRequestBase(GetServiceCapabilities, auth::SECURITY_LEVELS::PRE_AUTH, xs, configs),
				m_device_service_configs(device_service_configs)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		const auto& services = m_device_service_configs.find("GetServices");
		const auto& analytics_services_info = std::ranges::find_if(services->second, [](const auto& pt) {
			return pt.second.get<std::string>("namespace") == "http://www.onvif.org/ver20/analytics/wsdl";
		});
		auto& caps = analytics_services_info->second.get_child("Capabilities");

		pt::ptree xmlCapabilitiesNode;
		for (const auto& [key, node] : caps)
		{
			xmlCapabilitiesNode.add("<xmlattr>." + key, node.get_value<std::string>(""));
		}

		auto envelope_tree = utility::soap::getEnvelopeTree(ns_);
		envelope_tree.add_child("s:Body.tan:GetServiceCapabilitiesResponse.tan:Capabilities", xmlCapabilitiesNode);

		pt::ptree root_tree;
		root_tree.put_child("s:Envelope", envelope_tree);

		std::ostringstream os;
		pt::write_xml(os, root_tree);

		utility::http::fillResponseWithHeaders(*response, os.str());
	}
};

} // namespace analytics

AnalyticsService::AnalyticsService(const std::string& service_uri, const std::string& service_name,
																	 std::shared_ptr<IOnvifServer> srv)
		: IOnvifService(service_uri, service_name, srv)
{
	requestHandlers_.push_back(
			std::make_shared<analytics::GetServiceCapabilitiesHandler>(xml_namespaces_, configs_ptree_, *srv->DeviceService()->Configs()));
}

} // namespace osrv
