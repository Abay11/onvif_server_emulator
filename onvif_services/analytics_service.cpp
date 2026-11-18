#include "analytics_service.h"

#include "../Logger.h"
#include "../Server.h"

#include "../onvif/OnvifRequest.h"
#include "../onvif_services/device_service.h"
#include "../utility/AnalyticsReader.h"
#include "../utility/MediaProfilesManager.h"
#include "../utility/AnalyticsRules.h"

#include "../utility/HttpHelper.h"
#include "../utility/SoapHelper.h"
#include "../utility/XmlParser.h"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

namespace pt = boost::property_tree;

const std::string GetServiceCapabilities = "GetServiceCapabilities";

namespace osrv
{

namespace analytics
{

void fillModules(const boost::property_tree::ptree& modules, boost::property_tree::ptree& out, std::string_view xns)
{
	for (const auto& [key, node] : modules)
	{
		pt::ptree moduleNode;
		moduleNode.add("<xmlattr>.Name", node.get<std::string>("Name"));
		moduleNode.add("<xmlattr>.Type", node.get<std::string>("Type"));

		for (const auto& [paramKey, paramNode] : node.get_child("Parameters"))
		{
			// TODO: read depending on value type
			pt::ptree itemNode;
			itemNode.add("<xmlattr>.Name", paramKey);
			itemNode.add("<xmlattr>.Value", paramNode.get_value<int>());
			moduleNode.add_child("tt:Parameters.tt:SimpleItem", itemNode);
		}

		out.add_child(std::string{xns} + ":AnalyticsModule", moduleNode);
	}
};

void fillConfiguration(const pt::ptree& jsonConfigNode, pt::ptree& xmlConfigOut,
											 const utility::media::MediaProfilesManager& profilesMgr)
{
	auto cfgToken = jsonConfigNode.get<std::string>("token");
	xmlConfigOut.add("<xmlattr>.token", cfgToken);
	xmlConfigOut.add("tt:Name", jsonConfigNode.get<std::string>("Name"));
	xmlConfigOut.add("tt:UseCount", profilesMgr.GetUseCount(
																			cfgToken, osrv::CONFIGURATION_ENUMERATION[osrv::CONFIGURATION_TYPE::ANALYTICS]));

	utility::AnalyticsModulesReaderByConfigToken reader{cfgToken, profilesMgr.ReaderWriter()->ConfigsTree()};
	pt::ptree analyticsEngine;
	analytics::fillModules(reader.Modules(), analyticsEngine, "tt");

	xmlConfigOut.add_child("tt:AnalyticsEngineConfiguration", analyticsEngine);
	pt::ptree rules;
	if (auto configured = jsonConfigNode.get_child_optional("rules"))
		for (const auto& [_, rule] : *configured)
			rules.add_child("tt:Rule", utility::AnalyticsRules::RuleXml(rule));
	xmlConfigOut.add_child("tt:RuleEngineConfiguration", rules);
}
struct RulesHandler : public OnvifRequestBase
{
	std::shared_ptr<utility::AnalyticsRules> detector_;
	RulesHandler(const std::string& operation, const std::map<std::string, std::string>& ns,
							 const std::shared_ptr<pt::ptree>& configs, std::shared_ptr<utility::AnalyticsRules> detector)
			: OnvifRequestBase(operation,
												 operation.starts_with("Get") ? auth::SECURITY_LEVELS::READ_MEDIA
																											: auth::SECURITY_LEVELS::ACTUATE,
												 ns, configs),
				detector_(std::move(detector))
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		auto envelope = utility::soap::getEnvelopeTree(ns_);
		bool fault = false;
		try
		{
			envelope.add_child("s:Body.tan:" + name() + "Response", detector_->Execute(name(), request->content.string()));
		}
		catch (const utility::RuleFault& error)
		{
			fault = true;
			envelope.put("s:Body.s:Fault.s:Code.s:Value", error.receiver ? "s:Receiver" : "s:Sender");
			envelope.put("s:Body.s:Fault.s:Code.s:Subcode.s:Value", error.receiver ? "ter:Action" : "ter:InvalidArgVal");
			envelope.put("s:Body.s:Fault.s:Code.s:Subcode.s:Subcode.s:Value", "ter:" + error.detail);
			envelope.put("s:Body.s:Fault.s:Reason.s:Text", error.what());
			envelope.put("s:Body.s:Fault.s:Reason.s:Text.<xmlattr>.xml:lang", "en");
		}
		pt::ptree document;
		document.add_child("s:Envelope", envelope);
		std::ostringstream out;
		pt::write_xml(out, document);
		if (fault)
			utility::http::fillResponseWithHeaders(*response, out.str(), utility::http::ClientErrorDefaultWriter);
		else
			utility::http::fillResponseWithHeaders(*response, out.str());
	}
};

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
			return pt.second.template get<std::string>("namespace") == "http://www.onvif.org/ver20/analytics/wsdl";
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
	requestHandlers_.push_back(std::make_shared<analytics::GetServiceCapabilitiesHandler>(
			xml_namespaces_, configs_ptree_, *srv->DeviceService()->Configs()));
	for (const auto* operation :
			 {"GetSupportedRules", "GetRuleOptions", "GetRules", "CreateRules", "ModifyRules", "DeleteRules"})
		requestHandlers_.push_back(
				std::make_shared<analytics::RulesHandler>(operation, xml_namespaces_, configs_ptree_, srv->Rules()));
}

} // namespace osrv
