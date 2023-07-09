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
const std::string GetSupportedAnalyticsModules = "GetSupportedAnalyticsModules";

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

struct GetSupportedAnalyticsModulesHandler : public OnvifRequestBase
{
private:
public:
	GetSupportedAnalyticsModulesHandler(const std::map<std::string, std::string>& xs,
																			const std::shared_ptr<pt::ptree>& configs)
			: OnvifRequestBase(GetSupportedAnalyticsModules, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		// TODO: configuration token in the request is ignored for now
		auto fillAnalyticsModulesResponse = [](const pt::ptree& configIn, pt::ptree& out) {
			out.add("tt:AnalyticsModuleContentSchemaLocation",
							configIn.get<std::string>("AnalyticsModuleContentSchemaLocation"));
			for (const auto& [key, tree] : configIn.get_child("modules"))
			{
				pt::ptree moduleDescr;
				moduleDescr.add("<xmlattr>.Name", tree.get<std::string>("Name"));
				moduleDescr.add("<xmlattr>.fixed", tree.get<bool>("fixed"));
				moduleDescr.add("<xmlattr>.maxInstances", tree.get<std::string>("maxInstances"));
				for (const auto& [paramsKey, paramsNode] : tree.get_child("Parameters"))
				{
					const auto& itemType = paramsNode.get<std::string>("DescriptionType");
					pt::ptree item;
					item.add("<xmlattr>.Name", paramsNode.get<std::string>("Name"));
					item.add("<xmlattr>.Type", paramsNode.get<std::string>("Type"));
					moduleDescr.add_child("tt:Parameters.tt:" + itemType, item);
				}

				for (const auto& [messKey, messNode] : tree.get_child("Messages"))
				{
					pt::ptree message;
					message.add("<xmlattr>.IsProperty", messNode.get<bool>("IsProperty"));
					for (const auto& [sourceKey, sourceNode] : messNode.get_child("Source"))
					{
						pt::ptree item;
						item.add("<xmlattr>.Name", sourceNode.get<std::string>("Name"));
						item.add("<xmlattr>.Type", sourceNode.get<std::string>("Type"));
						const auto& itemType = sourceNode.get<std::string>("DescriptionType");

						message.add_child("tt:Source.tt:" + itemType, item);
					}

					for (const auto& [dataKey, dataNode] : messNode.get_child("Data"))
					{
						pt::ptree data;
						data.add("<xmlattr>.Name", dataNode.get<std::string>("Name"));
						data.add("<xmlattr>.Type", dataNode.get<std::string>("Type"));
						const auto& itemType = dataNode.get<std::string>("DescriptionType");

						message.add_child("tt:Data.tt:" + itemType, data);
					
					}

					message.add("tt:ParentTopic", messNode.get<std::string>("ParentTopic"));

					moduleDescr.add_child("tt:Messages", message);
				}

				out.add_child("tt:AnalyticsModuleDescription", moduleDescr);
			}
		};

		const auto& modules = service_configs_->get_child("SupportedAnalyticsModules");
		pt::ptree supportedAnalyticsModules;
		fillAnalyticsModulesResponse(modules, supportedAnalyticsModules);

		auto envelope_tree = utility::soap::getEnvelopeTree(ns_);
		envelope_tree.add_child("s:Body.tan:GetSupportedAnalyticsModulesResponse.tan:SupportedAnalyticsModules", supportedAnalyticsModules);

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
	requestHandlers_.push_back(std::make_shared<analytics::GetServiceCapabilitiesHandler>(
			xml_namespaces_, configs_ptree_, *srv->DeviceService()->Configs()));
	requestHandlers_.push_back(
			std::make_shared<analytics::GetSupportedAnalyticsModulesHandler>(xml_namespaces_, configs_ptree_));
	;
}

} // namespace osrv
