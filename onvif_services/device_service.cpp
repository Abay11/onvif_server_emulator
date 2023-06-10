#include "device_service.h"

#include "../onvif/OnvifRequest.h"

#include "../Logger.h"
#include "../Server.h"
#include "../utility/HttpDigestHelper.h"
#include "../utility/HttpHelper.h"
#include "../utility/SoapHelper.h"
#include "../utility/XmlParser.h"

#include "../Simple-Web-Server/server_http.hpp"

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
										 const std::string& ip)
			: OnvifRequestBase(GetServices, auth::SECURITY_LEVELS::PRE_AUTH, xs, configs), ipv4_address_(ip)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		auto envelope_tree = utility::soap::getEnvelopeTree(ns_);

		auto services_config = service_configs_->get_child(GetServices);
		pt::ptree services_node;

		// here's Services are enumerates as array, so handle them manualy
		for (auto elements : services_config)
		{
			if (!elements.second.get<bool>("Enabled", true))
				continue;

			pt::ptree xml_service_node;
			xml_service_node.put("tds:Namespace", elements.second.get<std::string>("namespace"));
			xml_service_node.put("tds:XAddr", ipv4_address_ + elements.second.get<std::string>("XAddr"));
			if (elements.second.get<std::string>("namespace") == "http://www.onvif.org/ver20/ptz/wsdl")
			{
				xml_service_node.put("tds:Capabilities.tptz:Capabilities", "");
			}
			xml_service_node.put("tds:Version.tt:Major", elements.second.get<std::string>("Version.Major"));
			xml_service_node.put("tds:Version.tt:Minor", elements.second.get<std::string>("Version.Minor"));

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

DeviceService::DeviceService(const std::string& service_uri, const std::string& service_name,
														 std::shared_ptr<IOnvifServer> srv)
		: IOnvifService(service_uri, service_name, srv)
{
	requestHandlers_.push_back(std::make_shared<GetCapabilitiesHandler>(xml_namespaces_, configs_ptree_,
																																			*srv->ServerConfigs(), srv->ServerAddress()));
	requestHandlers_.push_back(std::make_shared<GetDeviceInformationHandler>(xml_namespaces_, configs_ptree_));
	requestHandlers_.push_back(std::make_shared<GetNetworkInterfacesHandler>(xml_namespaces_, configs_ptree_));
	requestHandlers_.push_back(std::make_shared<GetRelayOutputsHandler>(xml_namespaces_, configs_ptree_));
	requestHandlers_.push_back(
			std::make_shared<GetServicesHandler>(xml_namespaces_, configs_ptree_, srv->ServerAddress()));
	requestHandlers_.push_back(std::make_shared<GetScopesHandler>(xml_namespaces_, configs_ptree_));
	requestHandlers_.push_back(std::make_shared<GetSystemDateAndTimeHandler>(xml_namespaces_, configs_ptree_));
}
} // namespace osrv