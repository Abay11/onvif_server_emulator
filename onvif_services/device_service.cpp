#include "device_service.h"

#include "../onvif/OnvifRequest.h"

#include "../Logger.h"
#include "../Server.h"
#include "../utility/ApplicationReboot.h"
#include "../utility/HttpDigestHelper.h"
#include "../utility/HttpHelper.h"
#include "../utility/SoapHelper.h"
#include "../utility/XmlParser.h"

#include "../Simple-Web-Server/server_http.hpp"

#include <algorithm>

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

// List of implemented methods
const std::string GetCapabilities = "GetCapabilities";
const std::string GetDeviceInformation = "GetDeviceInformation";
const std::string GetNetworkInterfaces = "GetNetworkInterfaces";
const std::string GetRelayOutputs = "GetRelayOutputs";
const std::string GetServices = "GetServices";
const std::string GetScopes = "GetScopes";
const std::string GetSystemDateAndTime = "GetSystemDateAndTime";

namespace pt = boost::property_tree;

namespace osrv
{
namespace
{
pt::ptree AuthenticationCapabilities(const ServerConfigs& config)
{
	pt::ptree result, network, security, system;
	const bool digest = config.auth_scheme_ == AUTH_SCHEME::DIGEST || config.auth_scheme_ == AUTH_SCHEME::DIGEST_WSS;
	const bool wss = config.auth_scheme_ == AUTH_SCHEME::WSS || config.auth_scheme_ == AUTH_SCHEME::DIGEST_WSS;
	security.put("<xmlattr>.HttpDigest", digest ? "true" : "false");
	security.put("<xmlattr>.UsernameToken", wss ? "true" : "false");
	if (digest) security.put("<xmlattr>.HashingAlgorithms", "MD5 SHA-256 SHA-512-256");
	result.add_child("tds:Network", network);
	result.add_child("tds:Security", security);
	result.add_child("tds:System", system);
	return result;
}
}
struct GetServiceCapabilitiesHandler : public OnvifRequestBase
{
	GetServiceCapabilitiesHandler(const std::map<std::string, std::string>& xs,
		const std::shared_ptr<pt::ptree>& configs, const std::shared_ptr<ServerConfigs>& config)
		: OnvifRequestBase("GetServiceCapabilities", auth::SECURITY_LEVELS::PRE_AUTH, xs, configs), config_(config) {}
	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request>) override
	{
		auto envelope = utility::soap::getEnvelopeTree(ns_);
		envelope.add_child("s:Body.tds:GetServiceCapabilitiesResponse.tds:Capabilities", AuthenticationCapabilities(*config_));
		pt::ptree root;
		root.add_child("s:Envelope", envelope);
		std::ostringstream xml;
		pt::write_xml(xml, root);
		utility::http::fillResponseWithHeaders(*response, xml.str());
	}
	std::shared_ptr<ServerConfigs> config_;
};

struct GetCapabilitiesHandler : public OnvifRequestBase
{
	GetCapabilitiesHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs,
												 osrv::ServerConfigs& server_cfg, const std::string& server_address)
			: OnvifRequestBase(GetCapabilities, auth::SECURITY_LEVELS::PRE_AUTH, xs, configs), srv_cfgs_(server_cfg),
				srv_addr_(server_address)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		// this processor will add a full network address to service's paths from configs
		struct XAddrProcessor
		{
			XAddrProcessor(const std::string& addr) : address_(addr)
			{
			}

			void operator()(const std::string& element, std::string& elData)
			{
				if (element == "XAddr")
				{
					elData = address_ + elData;
				}
			}

			const std::string address_;
		};

		auto capabilities_config = service_configs_->get_child("GetCapabilities");
		pt::ptree capabilities_node;
		utility::soap::jsonNodeToXml(capabilities_config, capabilities_node, "tt", XAddrProcessor(srv_addr_));

		// here cound of DI is overrided dynamically depending on the actually count of DI in the config file
		capabilities_node.add("tt:Device.tt:IO.tt:InputConnectors", srv_cfgs_.digital_inputs_.size());

		auto envelope_tree = utility::soap::getEnvelopeTree(ns_);
		envelope_tree.add_child("s:Body.tds:GetCapabilitiesResponse.tds:Capabilities", capabilities_node);

		pt::ptree root_tree;
		root_tree.put_child("s:Envelope", envelope_tree);

		std::ostringstream os;
		pt::write_xml(os, root_tree);

		utility::http::fillResponseWithHeaders(*response, os.str());
	}

private:
	osrv::ServerConfigs& srv_cfgs_;
	const std::string srv_addr_;
};

struct GetDeviceInformationHandler : public OnvifRequestBase
{
	GetDeviceInformationHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs)
			: OnvifRequestBase(GetDeviceInformation, auth::SECURITY_LEVELS::READ_SYSTEM, xs, configs)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		auto envelope_tree = utility::soap::getEnvelopeTree(ns_);

		auto device_info_config = service_configs_->get_child("GetDeviceInformation");
		pt::ptree device_info_node;

		utility::soap::jsonNodeToXml(device_info_config, device_info_node, "tds");

		envelope_tree.add_child("s:Body.tds:GetDeviceInformationResponse", device_info_node);

		pt::ptree root_tree;
		root_tree.put_child("s:Envelope", envelope_tree);

		std::ostringstream os;
		pt::write_xml(os, root_tree);

		utility::http::fillResponseWithHeaders(*response, os.str());
	}
};

struct GetNetworkInterfacesHandler : public OnvifRequestBase
{
	GetNetworkInterfacesHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs)
			: OnvifRequestBase(GetNetworkInterfaces, auth::SECURITY_LEVELS::READ_SYSTEM, xs, configs)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		auto envelope_tree = utility::soap::getEnvelopeTree(ns_);

		auto network_interfaces_config = service_configs_->get_child("GetNetworkInterfaces");
		pt::ptree network_interfaces_node;

		// this processor will add a xml ns depending on element
		struct NsProcessor
		{
			void operator()(std::string& element, std::string& elData)
			{
				if (element == "NetworkInterfaces")
				{
					// currently implemented only Loopback address
					element = "tds:" + element;
				}
				else
				{
					element = "tt:" + element;
				}
			}
		};

		utility::soap::jsonNodeToXml(network_interfaces_config, network_interfaces_node, "", NsProcessor());

		envelope_tree.add_child("s:Body.tds:GetNetworkInterfacesResponse", network_interfaces_node);

		pt::ptree root_tree;
		root_tree.put_child("s:Envelope", envelope_tree);

		std::ostringstream os;
		pt::write_xml(os, root_tree);

		utility::http::fillResponseWithHeaders(*response, os.str());
	}
};

struct GetRelayOutputsHandler : public OnvifRequestBase
{
	GetRelayOutputsHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs)
			: OnvifRequestBase(GetRelayOutputs, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		auto envelope_tree = utility::soap::getEnvelopeTree(ns_);

		// TODO: here is just stub response
		envelope_tree.add("s:Body.tds:GetRelayOutputsResponse", "");

		pt::ptree root_tree;
		root_tree.put_child("s:Envelope", envelope_tree);

		std::ostringstream os;
		pt::write_xml(os, root_tree);

		utility::http::fillResponseWithHeaders(*response, os.str());
	}
};

struct GetServicesHandler : public OnvifRequestBase
{
	GetServicesHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs,
		const std::shared_ptr<IOnvifServer>& srv)
		: OnvifRequestBase(GetServices, auth::SECURITY_LEVELS::PRE_AUTH, xs, configs)
		, ipv4_address_(srv->ServerAddress()), server_config_(srv->GetServerConfigs())
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		pt::ptree xml_tree;
		auto request_str = request->content.string();
		std::istringstream is(request_str);
		pt::xml_parser::read_xml(is, xml_tree);

		auto isNeedIncludeCaps = exns::find_hierarchy("Envelope.Body.GetServices.IncludeCapability", xml_tree) == "true";

		auto envelope_tree = utility::soap::getEnvelopeTree(ns_);

		auto ipv4_bind = ipv4_address_;

		auto services_config = service_configs_->get_child(GetServices);
		pt::ptree services_node;

		// here's Services are enumerates as array, so handle them manualy
		for (const auto& [key, node] : services_config)
		{
			if (!node.get<bool>("Enabled", true))
				continue;

			pt::ptree xml_service_node;
			const auto& ns = node.get<std::string>("namespace");
			xml_service_node.put("tds:Namespace", ns);
			xml_service_node.put("tds:XAddr", ipv4_address_ + node.get<std::string>("XAddr"));

			static const std::unordered_map<std::string, std::string> maps = {
					{"http://www.onvif.org/ver20/analytics/wsdl", "tan"},
					{"http://www.onvif.org/ver20/ptz/wsdl", "tptz"}
			};

			pt::ptree def;
			if (const auto& caps = node.get_child("Capabilities", def); isNeedIncludeCaps && !caps.empty())
			{
				const auto& ns_tag = maps.at(ns);
				for (const auto& [caps_key, caps_value] : caps)
					xml_service_node.add(std::format("tds:Capabilities.{}:Capabilities.<xmlattr>.{}", ns_tag, caps_key),
															 caps_value.get_value<std::string>());
			}

			if (isNeedIncludeCaps && ns == "http://www.onvif.org/ver10/device/wsdl")
				xml_service_node.add_child("tds:Capabilities.tds:Capabilities", AuthenticationCapabilities(*server_config_));

			xml_service_node.put("tds:Version.tt:Major", node.get<std::string>("Version.Major"));
			xml_service_node.put("tds:Version.tt:Minor", node.get<std::string>("Version.Minor"));

			services_node.add_child("tds:Service", xml_service_node);
		}

		envelope_tree.add_child("s:Body.tds:GetServicesResponse", services_node);

		pt::ptree root_tree;
		root_tree.put_child("s:Envelope", envelope_tree);

		std::ostringstream os;
		pt::write_xml(os, root_tree);

		utility::http::fillResponseWithHeaders(*response, os.str());
	}

private:
	const std::string ipv4_address_;
	std::shared_ptr<ServerConfigs> server_config_;
};

struct GetScopesHandler : public OnvifRequestBase
{
	GetScopesHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs)
			: OnvifRequestBase(GetScopes, auth::SECURITY_LEVELS::READ_SYSTEM, xs, configs)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		auto envelope_tree = utility::soap::getEnvelopeTree(ns_);

		static const auto SCOPES_TREE = service_configs_->get_child("GetScopes");
		for (const auto& it : SCOPES_TREE)
		{
			pt::ptree scopes_tree;
			scopes_tree.put("tt:ScopeDef", "Fixed");
			scopes_tree.put("tt:ScopeItem", "onvif://www.onvif.org/" + it.first + "/" + it.second.get_value<std::string>());

			envelope_tree.add_child("s:Body.tds:GetScopesResponse", scopes_tree);
		}

		pt::ptree root_tree;
		root_tree.put_child("s:Envelope", envelope_tree);

		std::ostringstream os;
		pt::write_xml(os, root_tree);

		utility::http::fillResponseWithHeaders(*response, os.str());
	}
};

struct GetSystemDateAndTimeHandler : public OnvifRequestBase
{
	GetSystemDateAndTimeHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs)
			: OnvifRequestBase(GetSystemDateAndTime, auth::SECURITY_LEVELS::PRE_AUTH, xs, configs)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		auto envelope_tree = utility::soap::getEnvelopeTree(ns_);

		envelope_tree.put("s:Body.tds:GetSystemDateAndTimeResponse.tds:SystemDateAndTime.tt:DateTimeType", "NTP");
		envelope_tree.put("s:Body.tds:GetSystemDateAndTimeResponse.tds:SystemDateAndTime.tt:DaylightSavings", "false");

		pt::ptree root_tree;
		root_tree.put_child("s:Envelope", envelope_tree);

		std::ostringstream os;
		pt::write_xml(os, root_tree);

		utility::http::fillResponseWithHeaders(*response, os.str());
	}
};

namespace
{
// Resolve the three request elements with their in-scope namespace declarations.
// SystemReboot has an empty complex type: parameters and attributes are invalid.
bool IsSystemRebootRequest(const std::string& xml)
{
	pt::ptree tree;
	std::istringstream input(xml);
	pt::read_xml(input, tree, pt::xml_parser::trim_whitespace | pt::xml_parser::no_comments);
	std::map<std::string, std::string> namespaces;
	auto matches = [&](const auto& element, const std::string& local, const std::string& uri) {
		if (auto attrs = element.second.get_child_optional("<xmlattr>"))
			for (const auto& [key, value] : *attrs)
			{
				if (key == "xmlns")
					namespaces[""] = value.template get_value<std::string>();
				else if (key.starts_with("xmlns:"))
					namespaces[key.substr(6)] = value.template get_value<std::string>();
			}
		const auto colon = element.first.find(':');
		const auto prefix = colon == std::string::npos ? "" : element.first.substr(0, colon);
		return exns::get_element_without_ns(element.first) == local && namespaces[prefix] == uri;
	};
	if (tree.size() != 1 || !matches(tree.front(), "Envelope", "http://www.w3.org/2003/05/soap-envelope"))
		return false;
	const auto& envelope = tree.front().second;
	if (std::count_if(envelope.begin(), envelope.end(),
										[](const auto& node) { return exns::get_element_without_ns(node.first) == "Body"; }) != 1)
		return false;
	for (const auto& body : envelope)
	{
		if (exns::get_element_without_ns(body.first) != "Body")
			continue;
		if (!matches(body, "Body", "http://www.w3.org/2003/05/soap-envelope"))
			return false;
		unsigned operations = 0;
		for (const auto& operation : body.second)
		{
			if (operation.first == "<xmlattr>")
				continue;
			if (++operations != 1 || !matches(operation, "SystemReboot", SERVICE_URI::DEVICE) ||
					!operation.second.data().empty())
				return false;
			for (const auto& [key, value] : operation.second)
			{
				if (key != "<xmlattr>")
					return false;
				for (const auto& [attr, ignored] : value)
					if (attr != "xmlns" && !attr.starts_with("xmlns:"))
						return false;
			}
		}
		return operations == 1;
	}
	return false;
}
} // namespace

struct SystemRebootHandler : public OnvifRequestBase
{
	SystemRebootHandler(const std::map<std::string, std::string>& ns, const std::shared_ptr<pt::ptree>& configs,
											const std::shared_ptr<IOnvifServer>& server)
			: OnvifRequestBase("SystemReboot", auth::SECURITY_LEVELS::UNRECOVERABLE, ns, configs),
				server_configs_(server->GetServerConfigs()), logger_(server->Logger())
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		auto envelope = utility::soap::getEnvelopeTree(ns_);
		bool valid = false;
		try
		{
			valid = IsSystemRebootRequest(request->content.string());
		}
		catch (const pt::xml_parser_error&)
		{
		}
		if (!valid || !server_configs_->reboot)
		{
			envelope.put("s:Body.s:Fault.s:Code.s:Value", valid ? "s:Receiver" : "s:Sender");
			envelope.put("s:Body.s:Fault.s:Code.s:Subcode.s:Value", valid ? "ter:ActionNotSupported" : "ter:InvalidArgVal");
			envelope.put("s:Body.s:Fault.s:Reason.s:Text",
									 valid ? "Application reboot is unavailable" : "Expected an empty Device SystemReboot request");
			envelope.put("s:Body.s:Fault.s:Reason.s:Text.<xmlattr>.xml:lang", "en");
			pt::ptree root;
			root.put_child("s:Envelope", envelope);
			std::ostringstream xml;
			pt::write_xml(xml, root);
			response->write(valid ? SimpleWeb::StatusCode::server_error_internal_server_error
														: SimpleWeb::StatusCode::client_error_bad_request,
											xml.str(), {{"Content-Type", "application/soap+xml; charset=utf-8"}});
			return;
		}
		envelope.put("s:Body.tds:SystemRebootResponse.tds:Message", "Application is rebooting");
		pt::ptree root;
		root.put_child("s:Envelope", envelope);
		std::ostringstream xml;
		pt::write_xml(xml, root);
		response->close_connection_after_response = true;
		utility::http::fillResponseWithHeaders(*response, xml.str());
		RebootAfterResponse(response, server_configs_, logger_);
	}

private:
	std::shared_ptr<ServerConfigs> server_configs_;
	std::shared_ptr<ILogger> logger_;
};

DeviceService::DeviceService(const std::string& service_uri, const std::string& service_name,
														 std::shared_ptr<IOnvifServer> srv)
		: IOnvifService(service_uri, service_name, srv)
{
	requestHandlers_.push_back(
			std::make_shared<GetServiceCapabilitiesHandler>(xml_namespaces_, configs_ptree_, srv->GetServerConfigs()));
	requestHandlers_.push_back(std::make_shared<GetCapabilitiesHandler>(xml_namespaces_, configs_ptree_,
		*srv->GetServerConfigs(), srv->ServerAddress()));
	requestHandlers_.push_back(std::make_shared<GetDeviceInformationHandler>(xml_namespaces_, configs_ptree_));
	requestHandlers_.push_back(std::make_shared<GetNetworkInterfacesHandler>(xml_namespaces_, configs_ptree_));
	requestHandlers_.push_back(std::make_shared<GetRelayOutputsHandler>(xml_namespaces_, configs_ptree_));
	requestHandlers_.push_back(
			std::make_shared<GetServicesHandler>(xml_namespaces_, configs_ptree_, srv));
	requestHandlers_.push_back(std::make_shared<GetScopesHandler>(xml_namespaces_, configs_ptree_));
	requestHandlers_.push_back(std::make_shared<GetSystemDateAndTimeHandler>(xml_namespaces_, configs_ptree_));
	requestHandlers_.push_back(std::make_shared<SystemRebootHandler>(xml_namespaces_, configs_ptree_, srv));
}
} // namespace osrv