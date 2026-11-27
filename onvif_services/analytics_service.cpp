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

#include <algorithm>
#include <boost/algorithm/string/trim.hpp>
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
	fillConfiguration(jsonConfigNode, xmlConfigOut, profilesMgr.ReaderWriter()->ConfigsTree());
}
void fillConfiguration(const pt::ptree& jsonConfigNode, pt::ptree& xmlConfigOut, const pt::ptree& profiles)
{
	auto cfgToken = jsonConfigNode.get<std::string>("token");
	xmlConfigOut.add("<xmlattr>.token", cfgToken);
	xmlConfigOut.add("tt:Name", jsonConfigNode.get<std::string>("Name"));
	unsigned count = 0;
	for (const auto& [_, profile] : profiles.get_child("MediaProfiles"))
		if (profile.get<std::string>("Analytics", "") == cfgToken)
			++count;
	xmlConfigOut.add("tt:UseCount", count);
	utility::AnalyticsModulesReaderByConfigToken reader{cfgToken, profiles};
	pt::ptree analyticsEngine;
	analytics::fillModules(reader.Modules(), analyticsEngine, "tt");
	for (const auto& module : scene::Modules())
		if (utility::AnalyticsRules::SupportsModule(jsonConfigNode, module.name, true))
			analyticsEngine.add_child("tt:AnalyticsModule", scene::ModuleXml(module.name));
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

// The synthetic tracker is a fixed module; these operations describe its output,
// without advertising support for mutable analytics-module management.
struct SceneDescriptionHandler : public OnvifRequestBase
{
	utility::media::MediaProfilesManager& manager;
	std::shared_ptr<utility::AnalyticsRules> rules;
	SceneDescriptionHandler(const std::string& operation, const std::map<std::string, std::string>& ns,
													const std::shared_ptr<pt::ptree>& configs, utility::media::MediaProfilesManager& manager,
													std::shared_ptr<utility::AnalyticsRules> rules)
			: OnvifRequestBase(operation, auth::SECURITY_LEVELS::READ_MEDIA, ns, configs), manager(manager),
				rules(std::move(rules))
	{
	}
	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		pt::ptree doc;
		std::istringstream input(request->content.string());
		pt::read_xml(input, doc);
		auto operation = name();
		auto configuration = exns::find_hierarchy("Envelope.Body." + operation + ".ConfigurationToken", doc);
		pt::ptree result;
		if (operation == "GetAnalyticsModules" || operation == "GetSupportedAnalyticsModules")
			manager.GetConfigByToken(configuration, "Analytics");
		if (operation == "GetAnalyticsModules")
		{
			utility::AnalyticsModulesReaderByConfigToken reader(configuration, manager.ReaderWriter()->ConfigsTree());
			analytics::fillModules(reader.Modules(), result, "tan");
			const auto& selected = manager.GetConfigByToken(configuration, "Analytics");
			for (const auto& module : scene::Modules())
				if (utility::AnalyticsRules::SupportsModule(selected, module.name, true))
					result.add_child("tan:AnalyticsModule", scene::ModuleXml(module.name));
		}
		else if (operation == "GetSupportedAnalyticsModules")
		{
			const auto& selected = manager.GetConfigByToken(configuration, "Analytics");
			result.put_child("tan:SupportedAnalyticsModules", {});
			for (const auto& module : scene::Modules())
			{
				if (!utility::AnalyticsRules::SupportsModule(selected, module.name))
					continue;
				pt::ptree description;
				description.put("<xmlattr>.Name", module.type);
				description.put("<xmlattr>.fixed", true);
				description.put("tt:Parameters", "");
				result.add_child("tan:SupportedAnalyticsModules.tt:AnalyticsModuleDescription", description);
			}
		}
		else
		{
			std::string selectedModule;
			auto elements = exns::find_hierarchy_elements("Envelope.Body." + operation + ".Type", doc);
			if (!elements.empty())
			{
				// QName values use declarations inherited from their own request path.
				std::map<std::string, std::string> bindings;
				const pt::ptree* node = &doc;
				for (const auto& localName : {std::string("Envelope"), std::string("Body"), operation, std::string("Type")})
				{
					const pt::ptree* next = nullptr;
					for (const auto& [key, value] : *node)
						if (key.substr(key.find(':') == std::string::npos ? 0 : key.find(':') + 1) == localName)
						{
							next = &value;
							break;
						}
					if (!next)
						throw no_config();
					node = next;
					if (auto attributes = node->get_child_optional("<xmlattr>"))
						for (const auto& [attr, uri] : *attributes)
							if (attr == "xmlns")
								bindings[""] = uri.data();
							else if (attr.starts_with("xmlns:"))
								bindings[attr.substr(6)] = uri.data();
				}
				auto type = boost::algorithm::trim_copy(elements.front()->second.data());
				auto colon = type.find(':');
				auto prefix = colon == std::string::npos ? "" : type.substr(0, colon);
				auto local = colon == std::string::npos ? type : type.substr(colon + 1);
				auto found = std::find_if(scene::Modules().begin(), scene::Modules().end(),
																	[&](const auto& module) { return module.type == "osrv:" + local; });
				if (bindings[prefix] != scene::ModuleNamespace || found == scene::Modules().end())
				{
					auto envelope = utility::soap::getEnvelopeTree(ns_);
					envelope.put("s:Body.s:Fault.s:Code.s:Value", "s:Sender");
					envelope.put("s:Body.s:Fault.s:Code.s:Subcode.s:Value", "ter:InvalidArgVal");
					envelope.put("s:Body.s:Fault.s:Code.s:Subcode.s:Subcode.s:Value", "ter:TypeNotExistent");
					envelope.put("s:Body.s:Fault.s:Reason.s:Text", "Unsupported scene module type");
					envelope.put("s:Body.s:Fault.s:Reason.s:Text.<xmlattr>.xml:lang", "en");
					pt::ptree root;
					root.add_child("s:Envelope", envelope);
					std::ostringstream out;
					pt::write_xml(out, root);
					utility::http::fillResponseWithHeaders(*response, out.str(), utility::http::ClientErrorDefaultWriter);
					return;
				}
				selectedModule = found->name;
			}
			for (const auto& module : scene::Modules())
			{
				if (!selectedModule.empty() && selectedModule != module.name)
					continue;
				pt::ptree metadata;
				metadata.put("<xmlattr>.Type", module.type);
				metadata.add_child("tan:SampleFrame", scene::FrameXml(rules->SupportedScene(module.name)));
				result.add_child("tan:AnalyticsModule", metadata);
			}
		}
		auto envelope = utility::soap::getEnvelopeTree(ns_);
		envelope.add_child("s:Body.tan:" + operation + "Response", result);
		pt::ptree root;
		root.add_child("s:Envelope", envelope);
		std::ostringstream out;
		pt::write_xml(out, root);
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
	for (const auto* operation : {"GetAnalyticsModules", "GetSupportedAnalyticsModules", "GetSupportedMetadata"})
		requestHandlers_.push_back(std::make_shared<analytics::SceneDescriptionHandler>(
				operation, xml_namespaces_, configs_ptree_, *srv->MediaProfilesManager(), srv->Rules()));
	requestHandlers_.push_back(std::make_shared<analytics::GetServiceCapabilitiesHandler>(
			xml_namespaces_, configs_ptree_, *srv->DeviceService()->Configs()));
	for (const auto* operation :
			 {"GetSupportedRules", "GetRuleOptions", "GetRules", "CreateRules", "ModifyRules", "DeleteRules"})
		requestHandlers_.push_back(
				std::make_shared<analytics::RulesHandler>(operation, xml_namespaces_, configs_ptree_, srv->Rules()));
}

} // namespace osrv
