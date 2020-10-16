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

using utility::http::SessionInfoHolder;

namespace osrv
{
	namespace device
	{
		//using handler_t = void(std::shared_ptr<HttpServer::Response> response,
		//	std::shared_ptr<HttpServer::Request> request,
		//	const SessionInfoHolder& sessionInfo);

#define OVERLOAD_HANDLER void operator()(std::shared_ptr<HttpServer::Response> response, \
		std::shared_ptr<HttpServer::Request> request, const SessionInfoHolder& sessionInfo) override

		struct RequestHandlerBase
		{
			RequestHandlerBase(const std::string& name, osrv::auth::SECURITY_LEVELS lvl) :
				name_(name),
				security_level_(lvl)
			{
			}

			virtual void operator()(std::shared_ptr<HttpServer::Response> response,
				std::shared_ptr<HttpServer::Request> request, const SessionInfoHolder& sessionInfo)
			{
				throw std::exception("Method is not implemented");
			}

			std::string get_name() const
			{
				return name_;
			}

			osrv::auth::SECURITY_LEVELS get_security_level()
			{
				return security_level_;
			}

		private:
			//Method name should be exactly match the name in the specification
			std::string name_;

			osrv::auth::SECURITY_LEVELS security_level_;
		};
		using HandlerSP = std::shared_ptr<RequestHandlerBase>;

		static std::vector<HandlerSP> handlers;

		struct GetCapabilitiesHandler : public RequestHandlerBase
		{
			GetCapabilitiesHandler() : RequestHandlerBase("GetCapabilities", osrv::auth::SECURITY_LEVELS::PRE_AUTH)
			{
			}

			OVERLOAD_HANDLER
			{
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
		};

		struct GetDeviceInformationHandler : public RequestHandlerBase
		{
			GetDeviceInformationHandler() : RequestHandlerBase(GetDeviceInformation, osrv::auth::SECURITY_LEVELS::READ_SYSTEM)
			{
			}

			OVERLOAD_HANDLER
			{
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
		};

		struct GetNetworkInterfacesHandler : public RequestHandlerBase
		{
			GetNetworkInterfacesHandler() : RequestHandlerBase(GetNetworkInterfaces, osrv::auth::SECURITY_LEVELS::READ_SYSTEM)
			{
			}

			OVERLOAD_HANDLER
			{
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
		};

		struct GetServicesHandler : public RequestHandlerBase
		{
			GetServicesHandler() : RequestHandlerBase(GetServices, osrv::auth::SECURITY_LEVELS::PRE_AUTH)
			{
			}

			OVERLOAD_HANDLER
			{
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
		};


		struct GetScopesHandler : public RequestHandlerBase
		{
			GetScopesHandler() : RequestHandlerBase(GetScopes, osrv::auth::SECURITY_LEVELS::READ_SYSTEM)
			{
			}

			OVERLOAD_HANDLER
			{
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
		};

		struct GetSystemDateAndTimeHandler : public RequestHandlerBase
		{
			GetSystemDateAndTimeHandler() : RequestHandlerBase("GetSystemDateAndTime", osrv::auth::SECURITY_LEVELS::PRE_AUTH)
			{
			}

			OVERLOAD_HANDLER
			{
				auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);

				envelope_tree.put("s:Body.tds:GetSystemDateAndTimeResponse.tds:SystemDateAndTime.tt:DateTimeType", "NTP");
				envelope_tree.put("s:Body.tds:GetSystemDateAndTimeResponse.tds:SystemDateAndTime.tt:DaylightSavings", "false");

				pt::ptree root_tree;
				root_tree.put_child("s:Envelope", envelope_tree);

				std::ostringstream os;
				pt::write_xml(os, root_tree);

				utility::http::fillResponseWithHeaders(*response, os.str());
			}
		};

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

			auto handler_it = std::find_if(handlers.begin(), handlers.end(),
				[&method](const HandlerSP handler) {
					return handler->get_name() == method;
				});

			SessionInfoHolder session_info;

			//handle requests
			if (handler_it != handlers.end())
			{
				try
				{
					auto handler_ptr = *handler_it;
					log_->Debug("Handling DeviceService request: " + handler_ptr->get_name());
					(*handler_ptr)(response, request, session_info);
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

			handlers.emplace_back(new GetCapabilitiesHandler());
			handlers.emplace_back(new GetDeviceInformationHandler());
			handlers.emplace_back(new GetNetworkInterfacesHandler());
			handlers.emplace_back(new GetServicesHandler());
			handlers.emplace_back(new GetScopesHandler());
			handlers.emplace_back(new GetSystemDateAndTimeHandler());

			srv.resource["/onvif/device_service"]["POST"] = DeviceServiceHandler;
		}

	}
}