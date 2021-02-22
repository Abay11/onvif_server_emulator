#include "device_service.h"

#include "../Logger.h"
#include "../Server.h"
#include "../utility/XmlParser.h"
#include "../utility/HttpHelper.h"
#include "../utility/SoapHelper.h"
#include "../utility/HttpDigestHelper.h"

#include "../Simple-Web-Server/server_http.hpp"

#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <map>

static ILogger* logger_ = nullptr;

static osrv::ServerConfigs* server_configs;
static DigestSessionSP digest_session;

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
static std::string SERVER_ADDRESS; // will be initiated in @init_service()

namespace osrv
{
	namespace device
	{
		const std::string CONFIGS_FILE = "device.config";

		//TODO:: Need release
		static std::vector<utility::http::HandlerSP> handlers;
		
		void do_handler_request(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request);

		struct GetCapabilitiesHandler : public utility::http::RequestHandlerBase
		{
			GetCapabilitiesHandler() : utility::http::RequestHandlerBase("GetCapabilities", osrv::auth::SECURITY_LEVELS::PRE_AUTH)
			{
			}

			OVERLOAD_REQUEST_HANDLER
			{
				auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);

				//this processor will add a full network address to service's paths from configs
				struct XAddrProcessor
				{
					void operator()(const std::string& element, std::string& elData)
					{
						if (element == "XAddr")
						{
							elData = SERVER_ADDRESS + elData;
						}
					}
				};

				auto capabilities_config = CONFIGS_TREE.get_child("GetCapabilities");
				pt::ptree capabilities_node;
				utility::soap::jsonNodeToXml(capabilities_config, capabilities_node, "tt", XAddrProcessor());
				
				// here cound of DI is overrided dynamically depending on the actually count of DI in the config file
				capabilities_node.add("tt:Device.tt:IO.tt:InputConnectors", server_configs->digital_inputs_.size());

				envelope_tree.add_child("s:Body.tds:GetCapabilitiesResponse.tds:Capabilities", capabilities_node);

				pt::ptree root_tree;
				root_tree.put_child("s:Envelope", envelope_tree);

				std::ostringstream os;
				pt::write_xml(os, root_tree);

				utility::http::fillResponseWithHeaders(*response, os.str());
			}
		};

		struct GetDeviceInformationHandler : public utility::http::RequestHandlerBase
		{
			GetDeviceInformationHandler() : utility::http::RequestHandlerBase(GetDeviceInformation, osrv::auth::SECURITY_LEVELS::READ_SYSTEM)
			{
			}

			OVERLOAD_REQUEST_HANDLER
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

		struct GetNetworkInterfacesHandler : public utility::http::RequestHandlerBase
		{
			GetNetworkInterfacesHandler() : utility::http::RequestHandlerBase(GetNetworkInterfaces, osrv::auth::SECURITY_LEVELS::READ_SYSTEM)
			{
			}

			OVERLOAD_REQUEST_HANDLER
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

		struct GetServicesHandler : public utility::http::RequestHandlerBase
		{
			GetServicesHandler() : utility::http::RequestHandlerBase(GetServices, osrv::auth::SECURITY_LEVELS::PRE_AUTH)
			{
			}

			OVERLOAD_REQUEST_HANDLER
			{
				auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);

				auto services_config = CONFIGS_TREE.get_child(GetServices);
				pt::ptree services_node;

				//here's Services are enumerates as array, so handle them manualy
				for (auto elements : services_config)
				{
					if (!elements.second.get<bool>("Enabled", true))
						continue;

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


		struct GetScopesHandler : public utility::http::RequestHandlerBase
		{
			GetScopesHandler() : utility::http::RequestHandlerBase(GetScopes, osrv::auth::SECURITY_LEVELS::READ_SYSTEM)
			{
			}

			OVERLOAD_REQUEST_HANDLER
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

		struct GetSystemDateAndTimeHandler : public utility::http::RequestHandlerBase
		{
			GetSystemDateAndTimeHandler() : utility::http::RequestHandlerBase("GetSystemDateAndTime", osrv::auth::SECURITY_LEVELS::PRE_AUTH)
			{
			}

			OVERLOAD_REQUEST_HANDLER
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
			if (auto delay = server_configs->network_delay_simulation_; delay > 0)
			{
				auto timer = std::make_shared<boost::asio::deadline_timer>(*server_configs->io_context_,
					boost::posix_time::milliseconds(delay));
				timer->async_wait(
					[timer, response, request](const boost::system::error_code& ec)
					{
						if (ec)
							return;

						do_handler_request(response, request);
					}
				);

			}
			else
			{
				do_handler_request(response, request);
			}
		}

		void do_handler_request(std::shared_ptr<HttpServer::Response> response,
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
				logger_->Error(e.what());
			}

			auto handler_it = std::find_if(handlers.begin(), handlers.end(),
				[&method](const utility::http::HandlerSP handler) {
					return handler->get_name() == method;
				});

			//handle requests
			if (handler_it != handlers.end())
			{
				//TODO: Refactor and take out to general place this authentication logic
				//check user credentials
				try
				{
					auto handler_ptr = *handler_it;
					logger_->Debug("Handling DeviceService request: " + handler_ptr->get_name());

					//extract user credentials
					osrv::auth::USER_TYPE current_user = osrv::auth::USER_TYPE::ANON;
					if (server_configs->auth_scheme_ == AUTH_SCHEME::DIGEST)
					{
						auto auth_header_it = request->header.find(utility::http::HEADER_AUTHORIZATION);
						if (auth_header_it != request->header.end())
						{
							//do extract user creds
							auto da_from_request = utility::digest::extract_DA(auth_header_it->second);

							bool isStaled;
							auto isCredsOk = digest_session->verifyDigest(da_from_request, isStaled);

							//if provided credentials are OK, upgrade UserType from Anon to appropriate Type
							if (isCredsOk)
							{
								current_user = osrv::auth::get_usertype_by_username(da_from_request.username, digest_session->get_users_list());
							}
						}
					
						if (!osrv::auth::isUserHasAccess(current_user, handler_ptr->get_security_level()))
						{
							throw osrv::auth::digest_failed{};
						}
					}

					(*handler_ptr)(response, request);
				}
				catch (const osrv::auth::digest_failed& e)
				{
					logger_->Error(e.what());
					
					*response << utility::http::RESPONSE_UNAUTHORIZED << "\r\n"
						<< "Content-Type: application/soap+xml; charset=utf-8" << "\r\n"
						<< "Content-Length: " << 0 << "\r\n"
						<< utility::http::HEADER_WWW_AUTHORIZATION << ": " << digest_session->generateDigest().to_string() << "\r\n"
						<< "\r\n";
				}
				catch (const std::exception& e)
				{
					logger_->Error("A server's error occured in DeviceService while processing: " + method
						+ ". Info: " + e.what());
					
					*response << "HTTP/1.1 500 Server error\r\nContent-Length: " << 0 << "\r\n\r\n";
				}
			}
			else
			{
				logger_->Error("Not found an appropriate handler in DeviceService for: " + method);
				*response << "HTTP/1.1 400 Bad request\r\nContent-Length: " << 0 << "\r\n\r\n";
			}

		}

		void init_service(HttpServer& srv, osrv::ServerConfigs& server_configs_instance, const std::string& configs_path, ILogger& logger)
		{
			if (logger_ != nullptr)
				return logger_->Error("DeviceService is already initiated!");

			logger_ = &logger;
			logger_->Debug("Initiating Device service...");

			server_configs = &server_configs_instance;
			digest_session = server_configs_instance.digest_session_;

			CONFIGS_PATH = configs_path;

			//getting service's configs
			pt::read_json(configs_path + CONFIGS_FILE, CONFIGS_TREE);

			auto namespaces_tree = CONFIGS_TREE.get_child("Namespaces");
			for (const auto& n : namespaces_tree)
				XML_NAMESPACES.insert({ n.first, n.second.get_value<std::string>() });

			server_configs->digital_inputs_ = read_digital_inputs(CONFIGS_TREE.get_child("DigitalInputs"));

			handlers.emplace_back(new GetCapabilitiesHandler());
			handlers.emplace_back(new GetDeviceInformationHandler());
			handlers.emplace_back(new GetNetworkInterfacesHandler());
			handlers.emplace_back(new GetServicesHandler());
			handlers.emplace_back(new GetScopesHandler());
			handlers.emplace_back(new GetSystemDateAndTimeHandler());

			srv.resource["/onvif/device_service"]["POST"] = DeviceServiceHandler;

			SERVER_ADDRESS = "http://";
			SERVER_ADDRESS += server_configs->ipv4_address_ + ":";
			SERVER_ADDRESS += server_configs->enabled_http_port_forwarding
				? std::to_string(server_configs->forwarded_http_port)
				: server_configs->http_port_;
			SERVER_ADDRESS += "/";

			logger_->Info("ONVIF Device service is working on " + SERVER_ADDRESS + "onvif/device_service");
		}

		const boost::property_tree::ptree& get_configs_tree_instance()
		{
			return CONFIGS_TREE;
		}

	} //device ns
}