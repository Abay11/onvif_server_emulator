#include "ptz_service.h"

#include "../Logger.h"
#include "../Server.h"

#include "../onvif/OnvifRequest.h"
#include "../utility/MediaProfilesManager.h"

#include "../utility/HttpHelper.h"
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
const std::string GetNodes = "GetNodes";
const std::string GetNode = "GetNode";
const std::string RelativeMove = "RelativeMove";
const std::string SetConfiguration = "SetConfiguration";
const std::string Stop = "Stop";

namespace osrv
{
namespace ptz
{

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
	GetCompatibleConfigurationsHandler(const std::map<std::string, std::string>& xs,
																		 const std::shared_ptr<pt::ptree>& configs)
			: OnvifRequestBase(GetCompatibleConfigurations, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		auto envelope_tree = utility::soap::getEnvelopeTree(ns_);

		// TODO: impl. reading configs from a file
		pt::ptree response_node;

		{
			pt::ptree ptz_node;
			ptz_node.add("<xmlattr>.token", "PtzConfigToken0");
			ptz_node.add("tt:Name", "PtzConfig0");
			ptz_node.add("tt:UseCount", 3);
			ptz_node.add("tt:NodeToken", "PTZNODE_1");
			ptz_node.add("tt:DefaultContinuousPanTiltVelocitySpace",
									 "http://www.onvif.org/ver10/tptz/PanTiltSpaces/VelocityGenericSpace");
			ptz_node.add("tt:DefaultContinuousZoomVelocitySpace",
									 "http://www.onvif.org/ver10/tptz/ZoomSpaces/VelocityGenericSpace");

			response_node.add_child("PTZConfiguration", ptz_node);
		}

		envelope_tree.add_child("s:Body.tptz:GetCompatibleConfigurationsResponse", response_node);

		pt::ptree root_tree;
		root_tree.put_child("s:Envelope", envelope_tree);

		std::ostringstream os;
		pt::write_xml(os, root_tree);

		utility::http::fillResponseWithHeaders(*response, os.str());
	}
};

struct GetConfigurationHandler : public OnvifRequestBase
{
	GetConfigurationHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs)
			: OnvifRequestBase(GetConfiguration, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		auto envelope_tree = utility::soap::getEnvelopeTree(ns_);

		// TODO: impl. reading configs from a file
		pt::ptree response_node;

		{
			pt::ptree ptz_node;
			ptz_node.add("<xmlattr>.token", "PtzConfigToken0");
			ptz_node.add("tt:Name", "PtzConfig0");
			ptz_node.add("tt:UseCount", 3);
			ptz_node.add("tt:NodeToken", "PTZNODE_1");
			ptz_node.add("tt:DefaultContinuousPanTiltVelocitySpace",
									 "http://www.onvif.org/ver10/tptz/PanTiltSpaces/VelocityGenericSpace");
			ptz_node.add("tt:DefaultContinuousZoomVelocitySpace",
									 "http://www.onvif.org/ver10/tptz/ZoomSpaces/VelocityGenericSpace");

			response_node.add_child("PTZConfiguration", ptz_node);
		}

		envelope_tree.add_child("s:Body.tptz:GetConfigurationResponse", response_node);

		pt::ptree root_tree;
		root_tree.put_child("s:Envelope", envelope_tree);

		std::ostringstream os;
		pt::write_xml(os, root_tree);

		utility::http::fillResponseWithHeaders(*response, os.str());
	}
};

struct GetConfigurationsHandler : public OnvifRequestBase
{
	GetConfigurationsHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs)
			: OnvifRequestBase(GetConfigurations, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		auto envelope_tree = utility::soap::getEnvelopeTree(ns_);

		// TODO: impl. reading configs from a file
		pt::ptree response_node;

		{
			pt::ptree ptz_node;
			ptz_node.add("<xmlattr>.token", "PtzConfigToken0");
			ptz_node.add("tt:Name", "PtzConfig0");
			ptz_node.add("tt:UseCount", 3);
			ptz_node.add("tt:NodeToken", "PTZNODE_1");
			ptz_node.add("tt:DefaultContinuousPanTiltVelocitySpace",
									 "http://www.onvif.org/ver10/tptz/PanTiltSpaces/VelocityGenericSpace");
			ptz_node.add("tt:DefaultContinuousZoomVelocitySpace",
									 "http://www.onvif.org/ver10/tptz/ZoomSpaces/VelocityGenericSpace");

			response_node.add_child("PTZConfiguration", ptz_node);
		}

		envelope_tree.add_child("s:Body.tptz:GetConfigurationsResponse", response_node);

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
	SetConfigurationHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs)
			: OnvifRequestBase(SetConfiguration, auth::SECURITY_LEVELS::ACTUATE, xs, configs)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		auto envelope_tree = utility::soap::getEnvelopeTree(ns_);

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
	requestHandlers_.push_back(
			std::make_shared<ptz::GetCompatibleConfigurationsHandler>(xml_namespaces_, configs_ptree_));
	requestHandlers_.push_back(std::make_shared<ptz::GetConfigurationHandler>(xml_namespaces_, configs_ptree_));
	requestHandlers_.push_back(std::make_shared<ptz::GetConfigurationsHandler>(xml_namespaces_, configs_ptree_));
	requestHandlers_.push_back(std::make_shared<ptz::GetNodeHandler>(xml_namespaces_, configs_ptree_));
	requestHandlers_.push_back(std::make_shared<ptz::GetNodesHandler>(xml_namespaces_, configs_ptree_));
	requestHandlers_.push_back(std::make_shared<ptz::RelativeMoveHandler>(xml_namespaces_, configs_ptree_));
	requestHandlers_.push_back(std::make_shared<ptz::SetConfigurationHandler>(xml_namespaces_, configs_ptree_));
	requestHandlers_.push_back(std::make_shared<ptz::StopHandler>(xml_namespaces_, configs_ptree_));
}

} // namespace osrv