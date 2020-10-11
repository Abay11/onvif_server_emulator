#include "device_service.h"

#include "../Logger.hpp"
#include "../utility/XmlParser.h"
#include "../utility/HttpHelper.h"
#include "../utility/SoapHelper.h"

#include "../Simple-Web-Server/server_http.hpp"

#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <map>

static Logger* log_ = nullptr;

//List of implemented methods
const std::string GetCapabilities = "GetCapabilities";
const std::string GetDeviceInformation = "GetDeviceInformation";
const std::string GetNetworkInterfaces = "GetNetworkInterfaces";
const std::string GetServices = "GetServices";
const std::string GetScopes = "GetScopes";
const std::string GetSystemDateAndTime = "GetSystemDateAndTime";

namespace pt = boost::property_tree;
static pt::ptree CONFIGS_TREE;
static osrv::StringsMap XML_NAMESPACES;

static std::string CONFIGS_PATH; //will be init with the service initialization
static const std::string DEVICE_CONFIGS_FILE = "device.config";

static std::string SERVER_ADDRESS = "http://127.0.0.1:8080/";

namespace osrv
{
	namespace device
	{
		using handler_t = void(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request);
		static std::map<std::string, handler_t*> handlers;

		void GetCapabilitiesHandler(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request)
		{
			log_->Debug("Handle GetCapabilities");

			auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);

			//this processor will add a full network address to service's paths from configs
			struct XAddrProcessor
			{
				void operator()(const std::string& element, std::string& elData)
				{
					if (element == "XAddr")
					{
						//currently implemented only Loopback address
						elData = SERVER_ADDRESS + elData;
					}
				}
			};

			auto capabilities_config = CONFIGS_TREE.get_child("GetCapabilities");
			pt::ptree capabilities_node;
			utility::soap::jsonNodeToXml(capabilities_config, capabilities_node, "tt", XAddrProcessor());

			envelope_tree.add_child("s:Body.tds:GetCapabilitiesResponse.tds:Capabilities", capabilities_node);

			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", envelope_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);

			utility::http::fillResponseWithHeaders(*response, os.str());
		}
		
		void GetDeviceInformationHandler(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request)
		{
			log_->Debug("Handle GetDeviceInformation");

			auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);

			auto device_info_config = CONFIGS_TREE.get_child("GetDeviceInformation");
			pt::ptree device_info_node;

			utility::soap::jsonNodeToXml(device_info_config, device_info_node, "tds");

			envelope_tree.add_child("s:Body.tds:GetDeviceInformationResponse", device_info_node);

			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", envelope_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);

			utility::http::fillResponseWithHeaders(*response, os.str());
		}

		void GetNetworkInterfacesHandler(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request)
		{
			log_->Debug("GetNetworkInterfaces");

			auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);

			auto network_interfaces_config = CONFIGS_TREE.get_child("GetNetworkInterfaces");
			pt::ptree network_interfaces_node;

			//this processor will add a xml ns depending on element
			struct NsProcessor
			{
				void operator()(std::string& element, std::string& elData)
				{
					if (element == "NetworkInterfaces")
					{
						//currently implemented only Loopback address
						element = "tds" + element;
					}
					else
					{
						element = "tt" + element;
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

		void GetServicesHandler(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request)
		{
			log_->Debug("Handle GetServices");
			
			auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);

			auto services_config = CONFIGS_TREE.get_child(GetServices);
			pt::ptree services_node;

			//here's Services are enumerates as array, so handle them manualy
			for (auto elements : services_config)
			{
				pt::ptree xml_service_node;
				xml_service_node.put("tds:Namespace", elements.second.get<std::string>("namespace"));
				xml_service_node.put("tds:XAddr", SERVER_ADDRESS + elements.second.get<std::string>("XAddr"));
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
		
		void GetScopesHandler(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request)
		{
			log_->Debug("Handle GetScopes");

			auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);

			static const auto SCOPES_TREE = CONFIGS_TREE.get_child("GetScopes");
			for (const auto& it : SCOPES_TREE)
			{
				pt::ptree scopes_tree;
				scopes_tree.put("tt:ScopeDef", "Fixed");
				scopes_tree.put("tt:ScopeItem",
					"onvif://www.onvif.org/" + it.first + "/" + it.second.get_value<std::string>());

				envelope_tree.add_child("s:Body.tds:GetScopesResponse", scopes_tree);
			}

			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", envelope_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);

			utility::http::fillResponseWithHeaders(*response, os.str());
		}

		void GetSystemDateAndTimeHandler(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request)
		{
			log_->Debug("Handle GetSystemDateAndTime");

			auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);
			
			envelope_tree.put("s:Body.tds:GetSystemDateAndTimeResponse.tds:SystemDateAndTime.tt:DateTimeType", "NTP");
			envelope_tree.put("s:Body.tds:GetSystemDateAndTimeResponse.tds:SystemDateAndTime.tt:DaylightSavings", "false");

			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", envelope_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);

			utility::http::fillResponseWithHeaders(*response, os.str());
		}
		
		//DEFAULT HANDLER
		void DeviceServiceHandler(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request)
		{
			//extract requested method
			std::string method;
			auto content = request->content.string();
			std::istringstream is(content);
			pt::ptree* tree = new exns::Parser();
			try
			{
				pt::xml_parser::read_xml(is, *tree);
				auto* ptr = static_cast<exns::Parser*>(tree);
				method = static_cast<exns::Parser*>(tree)->___getMethod();
			}
			catch (const pt::xml_parser_error& e)
			{
				log_->Error(e.what());
			}

			auto it = handlers.find(method);

			//handle requests
			if (it != handlers.end())
			{
				try
				{
					it->second(response, request);
				}
				catch (const std::exception& e)
				{
					log_->Error("A server's error occured in DeviceService while processing: " + method
						+ ". Info: " + e.what());
				
					*response << "HTTP/1.1 500 Server error\r\nContent-Length: " << 0 << "\r\n\r\n";
				}
			}
			else
			{
				log_->Error("Not found an appropriate handler in DeviceService for: " + method);
				*response << "HTTP/1.1 400 Bad request\r\nContent-Length: " << 0 << "\r\n\r\n";
			}
		}

		void init_service(HttpServer& srv, const std::string& configs_path, Logger& logger)
		{
			if (log_ != nullptr)
				return log_->Error("DeviceService is already inited!");

			log_ = &logger;

			log_->Debug("Initiating Device service...");

			CONFIGS_PATH = configs_path;

			//getting service's configs
			pt::read_json(configs_path + DEVICE_CONFIGS_FILE, CONFIGS_TREE);
			
			auto namespaces_tree = CONFIGS_TREE.get_child("Namespaces");
			for (const auto& n : namespaces_tree)
				XML_NAMESPACES.insert({ n.first, n.second.get_value<std::string>() });

			handlers.insert({ GetCapabilities, &GetCapabilitiesHandler });
			handlers.insert({ GetDeviceInformation, &GetDeviceInformationHandler });
			handlers.insert({ GetNetworkInterfaces, &GetNetworkInterfacesHandler });
			handlers.insert({ GetServices, &GetServicesHandler });
			handlers.insert({ GetScopes, &GetScopesHandler });
			handlers.insert({ GetSystemDateAndTime, &GetSystemDateAndTimeHandler });

			srv.resource["/onvif/device_service"]["POST"] = DeviceServiceHandler;
		}

	}
}