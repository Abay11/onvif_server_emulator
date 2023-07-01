#include "ptz_service.h"

#include "../Logger.h"
#include "../Server.h"

#include "../onvif/OnvifRequest.h"
#include "../utility/MediaProfilesManager.h"

#include "../utility/HttpHelper.h"
#include "../utility/PtzConfigurationReader.h"
#include "../utility/SoapHelper.h"
#include "../utility/XmlParser.h"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

namespace pt = boost::property_tree;

// a list of implemented methods
const std::string ContinuousMove = "ContinuousMove";
const std::string GetCompatibleConfigurations = "GetCompatibleConfigurations";
const std::string GetConfiguration = "GetConfiguration";
const std::string GetConfigurations = "GetConfigurations";
const std::string GetConfigurationOptions = "GetConfigurationOptions";
const std::string GetNode = "GetNode";
const std::string GetNodes = "GetNodes";
const std::string GetServiceCapabilities = "GetServiceCapabilities";
const std::string RelativeMove = "RelativeMove";
const std::string SetConfiguration = "SetConfiguration";
const std::string Stop = "Stop";

namespace osrv
{
namespace ptz
{
void fillPtzConfig(const pt::ptree& jsonConfigNode, pt::ptree& xmlConfigOut,
									 const utility::media::MediaProfilesManager& profilesMgr)
{
	auto cfgToken = jsonConfigNode.get<std::string>("token");
	xmlConfigOut.add("<xmlattr>.token", cfgToken);
	xmlConfigOut.add("tt:Name", jsonConfigNode.get<std::string>("Name"));
	xmlConfigOut.add("tt:UseCount",
									 profilesMgr.GetUseCount(cfgToken, osrv::CONFIGURATION_ENUMERATION[osrv::CONFIGURATION_TYPE::PTZ]));
	xmlConfigOut.add("tt:NodeToken", jsonConfigNode.get<std::string>("NodeToken"));

	xmlConfigOut.add("tt:DefaultContinuousPanTiltVelocitySpace",
									 jsonConfigNode.get<std::string>("DefaultContinuousPanTiltVelocitySpace"));
	xmlConfigOut.add("tt:DefaultContinuousZoomVelocitySpace",
									 jsonConfigNode.get<std::string>("DefaultContinuousZoomVelocitySpace"));

	/* If the PTZ Node supports absolute or relative PTZ movements, it shall specify corresponding default Pan/Tilt and
	Zoom speeds
	* So uncomment if absolute or relative PTZ movements will be implemented
	xmlConfigOut.add("tt:DefaultPTZSpeed.tt:PanTilt.<xmlattr>.x", jsonConfigNode.get<float>("DefaultPTZSpeed.PanTilt.x"));
	xmlConfigOut.add("tt:DefaultPTZSpeed.tt:PanTilt.<xmlattr>.y", jsonConfigNode.get<float>("DefaultPTZSpeed.PanTilt.y"));
	xmlConfigOut.add("tt:DefaultPTZSpeed.tt:PanTilt.<xmlattr>.space",
									 jsonConfigNode.get<std::string>("DefaultPTZSpeed.PanTilt.space"));
	xmlConfigOut.add("tt:DefaultPTZSpeed.tt:Zoom.<xmlattr>.x", jsonConfigNode.get<float>("DefaultPTZSpeed.Zoom.x"));
	xmlConfigOut.add("tt:DefaultPTZSpeed.tt:Zoom.<xmlattr>.space",
									 jsonConfigNode.get<std::string>("DefaultPTZSpeed.Zoom.space"));*/

	xmlConfigOut.add("tt:DefaultPTZTimeout", jsonConfigNode.get<std::string>("DefaultPTZTimeout"));

	/*	The Pan/Tilt limits element should be present for a PTZ Node that supports an absolute Pan/Tilt.
	*	If the element is present it signals the support for configurable Pan/Tilt limits.
	*	So uncomment if absolute Pan/Tilt will be supported
	xmlConfigOut.add("tt:PanTiltLimits.tt:Range.tt:URI", jsonConfigNode.get<std::string>("PanTiltLimits.Range.URI"));
	xmlConfigOut.add("tt:PanTiltLimits.tt:Range.tt:XRange.tt:Min",
									 jsonConfigNode.get<float>("PanTiltLimits.Range.XRange.Min"));
	xmlConfigOut.add("tt:PanTiltLimits.tt:Range.tt:XRange.tt:Max",
									 jsonConfigNode.get<float>("PanTiltLimits.Range.XRange.Max"));
	xmlConfigOut.add("tt:PanTiltLimits.tt:Range.tt:YRange.tt:Min",
									 jsonConfigNode.get<float>("PanTiltLimits.Range.YRange.Min"));
	xmlConfigOut.add("tt:PanTiltLimits.tt:Range.tt:YRange.tt:Max",
									 jsonConfigNode.get<float>("PanTiltLimits.Range.YRange.Max"));
									 */

	xmlConfigOut.add("tt:ZoomLimits.tt:Range.tt:URI", jsonConfigNode.get<std::string>("ZoomLimits.Range.URI"));
	xmlConfigOut.add("tt:ZoomLimits.tt:Range.tt:XRange.tt:Min", jsonConfigNode.get<float>("ZoomLimits.Range.XRange.Min"));
	xmlConfigOut.add("tt:ZoomLimits.tt:Range.tt:XRange.tt:Max", jsonConfigNode.get<float>("ZoomLimits.Range.XRange.Max"));
};

pt::ptree fillNode(const pt::ptree& nodeConfigJson)
{
	pt::ptree node_tree;
	node_tree.add("<xmlattr>.token", nodeConfigJson.get<std::string>("token"));
	node_tree.add("<xmlattr>.FixedHomePosition", nodeConfigJson.get<bool>("FixedHomePosition"));
	node_tree.add("<xmlattr>.GeoMove", nodeConfigJson.get<bool>("GeoMove"));
	node_tree.add("tt:Name", nodeConfigJson.get<std::string>("Name"));

	for (const auto& [key, spaceNode] : nodeConfigJson.get_child("SupportedPTZSpaces"))
	{
		const auto& space_name = spaceNode.get<std::string>("space");
		const auto item_path = "tt:SupportedPTZSpaces.tt:" + space_name;
		node_tree.add(item_path + ".tt:URI", spaceNode.get<std::string>("URI"));
		node_tree.add(item_path + ".tt:XRange.tt:Min", spaceNode.get<std::string>("XRange.Min"));
		node_tree.add(item_path + ".tt:XRange.tt:Max", spaceNode.get<std::string>("XRange.Max"));

		if (auto yrange = spaceNode.get_child("YRange", {}); !yrange.empty())
		{
			node_tree.add(item_path + ".tt:YRange.tt:Min", yrange.get<std::string>("Min"));
			node_tree.add(item_path + ".tt:YRange.tt:Max", yrange.get<std::string>("Max"));
		}
	}

	node_tree.add("tt:MaximumNumberOfPresets", nodeConfigJson.get<int>("MaximumNumberOfPresets"));
	node_tree.add("tt:HomeSupported", nodeConfigJson.get<bool>("HomeSupported"));

	return node_tree;
};

struct ContinuousMoveHandler : public OnvifRequestBase
{
	ContinuousMoveHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs)
			: OnvifRequestBase(ContinuousMove, auth::SECURITY_LEVELS::ACTUATE, xs, configs)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		auto envelope_tree = utility::soap::getEnvelopeTree(ns_);

		envelope_tree.add("s:Body.tptz:ContinuousMoveResponse", "");

		pt::ptree root_tree;
		root_tree.put_child("s:Envelope", envelope_tree);

		std::ostringstream os;
		pt::write_xml(os, root_tree);

		utility::http::fillResponseWithHeaders(*response, os.str());
	}
};

struct GetCompatibleConfigurationsHandler : public OnvifRequestBase
{
private:
	const utility::media::MediaProfilesManager& m_profilesMgr;

public:
	GetCompatibleConfigurationsHandler(const std::map<std::string, std::string>& xs,
																		 const std::shared_ptr<pt::ptree>& configs,
																		 const utility::media::MediaProfilesManager& profilesMgr)
			: OnvifRequestBase(GetCompatibleConfigurations, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs),
				m_profilesMgr(profilesMgr)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		pt::ptree xml_tree;
		auto request_str = request->content.string();
		std::istringstream is(request_str);
		pt::xml_parser::read_xml(is, xml_tree);

		std::string profileToken;
		profileToken = exns::find_hierarchy("Envelope.Body.GetCompatibleConfigurations.ProfileToken", xml_tree);

		const auto& profileConfig = m_profilesMgr.GetProfileByToken(profileToken);
		auto vsToken = profileConfig.get<std::string>("VideoSource");
		const auto& configs = m_profilesMgr.ReaderWriter()->ConfigsTree();

		const auto& vsConfigJson =
				m_profilesMgr.GetConfigByToken(vsToken, osrv::CONFIGURATION_ENUMERATION[osrv::VIDEOSOURCE]);

		const auto& compatibleNodes = vsConfigJson.get_child("CompatiblePtzNodes");
		std::vector<std::string> compatibleNodeTokens;
		std::ranges::transform(compatibleNodes, std::back_inserter(compatibleNodeTokens),
													 [](auto t) { return t.second.get_value<std::string>(); });

		const auto& allPtzConfigs =
				m_profilesMgr.ReaderWriter()->ConfigsTree().get_child(osrv::CONFIGURATION_ENUMERATION[osrv::PTZ]);

		auto envelope_tree = utility::soap::getEnvelopeTree(ns_);
		for (const auto& [key, node] : allPtzConfigs)
		{
			if (std::ranges::find_if(compatibleNodeTokens, [&node](const auto& nodeToken) {
						return node.get<std::string>("NodeToken") == nodeToken;
					}) == compatibleNodeTokens.end())
				continue; // current ptz configuration is not compatible with the requested media profile

			pt::ptree xmlPtzConfig;
			fillPtzConfig(node, xmlPtzConfig, m_profilesMgr);

			envelope_tree.add_child("s:Body.tptz:GetCompatibleConfigurationsResponse.tptz:PTZConfiguration", xmlPtzConfig);
		}

		pt::ptree root_tree;
		root_tree.put_child("s:Envelope", envelope_tree);

		std::ostringstream os;
		pt::write_xml(os, root_tree);

		utility::http::fillResponseWithHeaders(*response, os.str());
	}
};

struct GetConfigurationHandler : public OnvifRequestBase
{
private:
	const utility::media::MediaProfilesManager& m_profilesMgr;

public:
	GetConfigurationHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs,
													const utility::media::MediaProfilesManager& profilesMgr)
			: OnvifRequestBase(GetConfiguration, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs), m_profilesMgr(profilesMgr)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		pt::ptree request_xml_tree;
		pt::xml_parser::read_xml(request->content, request_xml_tree);
		auto requestedToken =
				exns::find_hierarchy("Envelope.Body.GetConfiguration.PTZConfigurationToken", request_xml_tree);

		pt::ptree configNode;
		fillPtzConfig(utility::PtzConfigsReaderByToken(requestedToken, m_profilesMgr.ReaderWriter()->ConfigsTree()).Ptz(),
									configNode, m_profilesMgr);

		auto envelope_tree = utility::soap::getEnvelopeTree(ns_);
		envelope_tree.add_child("s:Body.tptz:GetConfigurationResponse.tptz:PTZConfiguration", configNode);

		pt::ptree root_tree;
		root_tree.put_child("s:Envelope", envelope_tree);

		std::ostringstream os;
		pt::write_xml(os, root_tree);

		utility::http::fillResponseWithHeaders(*response, os.str());
	}
};

struct GetConfigurationsHandler : public OnvifRequestBase
{
private:
	const utility::media::MediaProfilesManager& m_profilesMgr;

public:
	GetConfigurationsHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs,
													 const utility::media::MediaProfilesManager& profilesMgr)
			: OnvifRequestBase(GetConfigurations, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs), m_profilesMgr(profilesMgr)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		const auto& ptzConfigsList =
				m_profilesMgr.ReaderWriter()->ConfigsTree().get_child(CONFIGURATION_ENUMERATION[CONFIGURATION_TYPE::PTZ]);

		pt::ptree response_node;
		for (const auto& [key, configNode] : ptzConfigsList)
		{
			pt::ptree ptzConfigResponseNode;
			fillPtzConfig(configNode, ptzConfigResponseNode, m_profilesMgr);
			response_node.add_child("tptz:PTZConfiguration", ptzConfigResponseNode);
		}

		auto envelope_tree = utility::soap::getEnvelopeTree(ns_);
		envelope_tree.add_child("s:Body.tptz:GetConfigurationsResponse", response_node);
		pt::ptree root_tree;
		root_tree.put_child("s:Envelope", envelope_tree);

		std::ostringstream os;
		pt::write_xml(os, root_tree);

		utility::http::fillResponseWithHeaders(*response, os.str());
	}
};

struct GetConfigurationOptionsHandler : public OnvifRequestBase
{
private:
	const utility::media::MediaProfilesManager& m_profilesMgr;

public:
	GetConfigurationOptionsHandler(const std::map<std::string, std::string>& xs,
																 const std::shared_ptr<pt::ptree>& configs,
																 const utility::media::MediaProfilesManager& profilesMgr)
			: OnvifRequestBase(GetConfigurationOptions, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs),
				m_profilesMgr(profilesMgr)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		pt::ptree request_xml_tree;
		pt::xml_parser::read_xml(request->content, request_xml_tree);
		auto requestedToken =
				exns::find_hierarchy("Envelope.Body.GetConfigurationOptions.ConfigurationToken", request_xml_tree);

		auto ptzConfig =
				utility::PtzConfigsReaderByToken(requestedToken, m_profilesMgr.ReaderWriter()->ConfigsTree()).Ptz();

		auto usedNodeTokenInPtzConfig = ptzConfig.get<std::string>("NodeToken", {});

		auto ptzNodesConfigJson = service_configs_->get_child("Nodes", {});
		const auto ptzNodeConfigJsonIt =
				std::ranges::find_if(ptzNodesConfigJson, [&usedNodeTokenInPtzConfig](const auto& it) {
					return it.second.get<std::string>("token") == usedNodeTokenInPtzConfig;
				});

		if (ptzNodeConfigJsonIt == ptzNodesConfigJson.end())
		{
			// this normally should not happen!! it means your configuraitons files invalid!!
			throw std::runtime_error("Failed to find related PTZ Node to PTZ configuration! Node token: " +
															 usedNodeTokenInPtzConfig);
		}

		auto ptzConfigOptions = m_profilesMgr.ReaderWriter()->ConfigsTree().get_child("PTZConfigurationOptions");
		auto currentPtzConfigOptionsIt = std::ranges::find_if(ptzConfigOptions, [&requestedToken](const auto& p) {
			return p.second.get<std::string>("token") == requestedToken;
		});
		if (currentPtzConfigOptionsIt == ptzConfigOptions.end())
		{
			// this normally should not happen!! it means your configuraitons files invalid!!
			throw std::runtime_error("Failed to find related PTZ configuration options! PTZ token: " + requestedToken);
		}

		pt::ptree PTZConfigurationOptionsNode;

		for (const auto& [key, node] : ptzNodeConfigJsonIt->second.get_child("SupportedPTZSpaces", {}))
		{
			pt::ptree spaceConfig;

			spaceConfig.add("tt:URI", node.get<std::string>("URI"));

			spaceConfig.add("tt:XRange.tt:Min", node.get<std::string>("XRange.Min"));
			spaceConfig.add("tt:XRange.tt:Max", node.get<std::string>("XRange.Max"));

			if (auto YRangeNodeJson = node.get_child("YRange", {}); !YRangeNodeJson.empty())
			{
				spaceConfig.add("tt:YRange.tt:Min", YRangeNodeJson.get<std::string>("Min"));
				spaceConfig.add("tt:YRange.tt:Max", YRangeNodeJson.get<std::string>("Max"));
			}

			PTZConfigurationOptionsNode.add_child("tt:Spaces.tt:" + node.get<std::string>("space"), spaceConfig);
		}

		PTZConfigurationOptionsNode.add("tt:PTZTimeout.tt:Min",
																		currentPtzConfigOptionsIt->second.get<std::string>("PTZTimeout.Min"));
		PTZConfigurationOptionsNode.add("tt:PTZTimeout.tt:Max",
																		currentPtzConfigOptionsIt->second.get<std::string>("PTZTimeout.Max"));

		auto envelope_tree = utility::soap::getEnvelopeTree(ns_);
		envelope_tree.add_child("s:Body.tptz:GetConfigurationOptionsResponse.tptz:PTZConfigurationOptions",
														PTZConfigurationOptionsNode);
		pt::ptree root_tree;
		root_tree.put_child("s:Envelope", envelope_tree);

		std::ostringstream os;
		pt::write_xml(os, root_tree);

		utility::http::fillResponseWithHeaders(*response, os.str());
	}
};

struct RelativeMoveHandler : public OnvifRequestBase
{
	RelativeMoveHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs)
			: OnvifRequestBase(RelativeMove, auth::SECURITY_LEVELS::ACTUATE, xs, configs)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		auto envelope_tree = utility::soap::getEnvelopeTree(ns_);

		envelope_tree.add("s:Body.tptz:RelativeMoveResponse", "");

		pt::ptree root_tree;
		root_tree.put_child("s:Envelope", envelope_tree);

		std::ostringstream os;
		pt::write_xml(os, root_tree);

		utility::http::fillResponseWithHeaders(*response, os.str());
	}
};

struct GetNodesHandler : public OnvifRequestBase
{
	GetNodesHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs)
			: OnvifRequestBase(GetNodes, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		auto envelope_tree = utility::soap::getEnvelopeTree(ns_);

		pt::ptree nodes_tree;

		auto nodes_config = service_configs_->get_child("Nodes");

		for (const auto& [key, node] : nodes_config)
		{
			nodes_tree.add_child("tptz:PTZNode", fillNode(node));
		}

		envelope_tree.add_child("s:Body.tptz:GetNodesResponse", nodes_tree);

		pt::ptree root_tree;
		root_tree.put_child("s:Envelope", envelope_tree);

		std::ostringstream os;
		pt::write_xml(os, root_tree);

		utility::http::fillResponseWithHeaders(*response, os.str());
	}
};

struct GetServiceCapabilitiesHandler : public OnvifRequestBase
{
	GetServiceCapabilitiesHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs)
			: OnvifRequestBase(GetServiceCapabilities, auth::SECURITY_LEVELS::PRE_AUTH, xs, configs)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		auto envelope_tree = utility::soap::getEnvelopeTree(ns_);

		envelope_tree.add("s:Body.tptz:GetServiceCapabilitiesResponse.tptz:Capabilities", "");

		pt::ptree root_tree;
		root_tree.put_child("s:Envelope", envelope_tree);

		std::ostringstream os;
		pt::write_xml(os, root_tree);

		utility::http::fillResponseWithHeaders(*response, os.str());
	}
};

struct GetNodeHandler : public OnvifRequestBase
{
	GetNodeHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs)
			: OnvifRequestBase(GetNode, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		auto envelope_tree = utility::soap::getEnvelopeTree(ns_);

		pt::ptree nodes_tree;

		auto nodes_config = service_configs_->get_child("Nodes");

		std::string requestedToken;
		{
			auto request_str = request->content.string();
			std::istringstream is(request_str);
			pt::ptree xml_tree;
			pt::xml_parser::read_xml(is, xml_tree);
			requestedToken = exns::find_hierarchy("Envelope.Body.GetNode.NodeToken", xml_tree);
		}

		auto nodeConfigIt = std::ranges::find_if(nodes_config, [&requestedToken](const auto nodesIt) {
			return nodesIt.second.get<std::string>("token") == requestedToken;
		});

		if (nodeConfigIt == nodes_config.end())
			throw osrv::no_entity();

		nodes_tree.add_child("tptz:PTZNode", fillNode(nodeConfigIt->second));

		envelope_tree.add_child("s:Body.tptz:GetNodeResponse", nodes_tree);

		pt::ptree root_tree;
		root_tree.put_child("s:Envelope", envelope_tree);

		std::ostringstream os;
		pt::write_xml(os, root_tree);

		utility::http::fillResponseWithHeaders(*response, os.str());
	}
};

struct SetConfigurationHandler : public OnvifRequestBase
{
private:
	utility::media::MediaProfilesManager& m_profilesMgr;

public:
	SetConfigurationHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs,
													utility::media::MediaProfilesManager& mgr)
			: OnvifRequestBase(SetConfiguration, auth::SECURITY_LEVELS::ACTUATE, xs, configs), m_profilesMgr(mgr)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		auto envelope_tree = utility::soap::getEnvelopeTree(ns_);

		pt::ptree requestXmlTree;
		pt::xml_parser::read_xml(request->content, requestXmlTree);
		auto ptzConfigRequestTree =
				exns::find_hierarchy_elements("Envelope.Body.SetConfiguration.PTZConfiguration", requestXmlTree);
		if (ptzConfigRequestTree.size() != size_t{1})
		{
			throw osrv::well_formed{};
		}

		auto requestedToken = ptzConfigRequestTree.front()->second.get<std::string>("<xmlattr>.token", {});

		auto& ptzConfigsJson =
				m_profilesMgr.ReaderWriter()->ConfigsTree().get_child(CONFIGURATION_ENUMERATION[CONFIGURATION_TYPE::PTZ]);

		auto configIt = std::ranges::find_if(
				ptzConfigsJson, [&requestedToken](auto& p) { return p.second.get<std::string>("token") == requestedToken; });

		if (configIt == ptzConfigsJson.end())
		{
			throw osrv::no_config{};
		}

		auto& ptzNodeJson = configIt->second;

		if (auto newDefaultPtzTimeout =
						exns::find_hierarchy("Envelope.Body.SetConfiguration.PTZConfiguration.DefaultPTZTimeout", requestXmlTree);
				!newDefaultPtzTimeout.empty())
		{
			ptzNodeJson.put("DefaultPTZTimeout", newDefaultPtzTimeout);
		}

		m_profilesMgr.ReaderWriter()->Save();

		envelope_tree.add("s:Body.tptz:SetConfigurationResponse", "");

		pt::ptree root_tree;
		root_tree.put_child("s:Envelope", envelope_tree);

		std::ostringstream os;
		pt::write_xml(os, root_tree);

		utility::http::fillResponseWithHeaders(*response, os.str());
	}
};

struct StopHandler : public OnvifRequestBase
{
	StopHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs)
			: OnvifRequestBase(Stop, auth::SECURITY_LEVELS::ACTUATE, xs, configs)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		auto envelope_tree = utility::soap::getEnvelopeTree(ns_);

		envelope_tree.add("s:Body.tptz:StopResponse", "");

		pt::ptree root_tree;
		root_tree.put_child("s:Envelope", envelope_tree);

		std::ostringstream os;
		pt::write_xml(os, root_tree);

		utility::http::fillResponseWithHeaders(*response, os.str());
	}
};

} // namespace ptz

PTZService::PTZService(const std::string& service_uri, const std::string& service_name,
											 std::shared_ptr<IOnvifServer> srv)
		: IOnvifService(service_uri, service_name, srv)
{
	requestHandlers_.push_back(std::make_shared<ptz::ContinuousMoveHandler>(xml_namespaces_, configs_ptree_));
	requestHandlers_.push_back(std::make_shared<ptz::GetCompatibleConfigurationsHandler>(xml_namespaces_, configs_ptree_,
																																											 *srv->MediaProfilesManager()));
	requestHandlers_.push_back(
			std::make_shared<ptz::GetConfigurationHandler>(xml_namespaces_, configs_ptree_, *srv->MediaProfilesManager()));
	requestHandlers_.push_back(
			std::make_shared<ptz::GetConfigurationsHandler>(xml_namespaces_, configs_ptree_, *srv->MediaProfilesManager()));
	requestHandlers_.push_back(std::make_shared<ptz::GetConfigurationOptionsHandler>(xml_namespaces_, configs_ptree_,
																																									 *srv->MediaProfilesManager()));
	requestHandlers_.push_back(std::make_shared<ptz::GetNodeHandler>(xml_namespaces_, configs_ptree_));
	requestHandlers_.push_back(std::make_shared<ptz::GetNodesHandler>(xml_namespaces_, configs_ptree_));
	requestHandlers_.push_back(std::make_shared<ptz::GetServiceCapabilitiesHandler>(xml_namespaces_, configs_ptree_));
	requestHandlers_.push_back(std::make_shared<ptz::RelativeMoveHandler>(xml_namespaces_, configs_ptree_));
	requestHandlers_.push_back(
			std::make_shared<ptz::SetConfigurationHandler>(xml_namespaces_, configs_ptree_, *srv->MediaProfilesManager()));
	requestHandlers_.push_back(std::make_shared<ptz::StopHandler>(xml_namespaces_, configs_ptree_));
}

} // namespace osrv