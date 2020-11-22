#include "event_service.h"

#include "../Logger.hpp"
#include "../Server.h"
#include "../utility/XmlParser.h"
#include "../utility/HttpHelper.h"
#include "../utility/SoapHelper.h"
#include "pull_point.h"

#include "../Simple-Web-Server/server_http.hpp"

#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <map>

static Logger* log_ = nullptr;

//this instance is required when handlers should be registered for a new PullPoint's subscriber
static osrv::HttpServer* http_server_intance = nullptr;

static const osrv::ServerConfigs* server_configs = nullptr;
static DigestSessionSP digest_session;

static std::unique_ptr<osrv::event::NotificationsManager> notifications_manager;

namespace pt = boost::property_tree;
static pt::ptree EVENT_CONFIGS_TREE;

static osrv::StringsMap XML_NAMESPACES;

static std::string CONFIGS_PATH; //will be init with the service initialization
static const std::string EVENT_CONFIGS_FILE = "event.config";

//List of implemented methods of Events service port
const std::string GetEventProperties = "GetEventProperties";

namespace osrv
{
	namespace event
	{
		static std::vector<utility::http::HandlerSP> handlers;

		//PullPoint handlers
		struct CreatePullPointSubscriptionHandler : public utility::http::RequestHandlerBase
		{
			CreatePullPointSubscriptionHandler() : utility::http::RequestHandlerBase("CreatePullPointSubscription",
				osrv::auth::SECURITY_LEVELS::READ_MEDIA)
			{
			}

			OVERLOAD_REQUEST_HANDLER
			{
				//TODO: Handler filters

				pt::ptree analytics_configs;
				auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);

				envelope_tree.add("s:Header.wsa:Action", "http://www.onvif.org/ver10/events/wsdl/EventPortType/CreatePullPointSubscriptionResponse");

				std::string sub_ref = "http://127.0.0.1:8080/";
				auto pullpoint = notifications_manager->CreatePullPoint();
				sub_ref += pullpoint->GetSubscriptionReference();

				pt::ptree response_node;
				response_node.add("tet:SubscriptionReference.wsa:Address", sub_ref);
				
				response_node.add("tet:SubscriptionReference.wsnt:CurrentTime", pullpoint->GetLastRenew());
				response_node.add("tet:SubscriptionReference.wsnt:TerminationTime", pullpoint->GetTerminationTime());

				envelope_tree.add_child("s:Body.tet:CreatePullPointSubscriptionResponse", response_node);

				pt::ptree root_tree;
				root_tree.put_child("s:Envelope", envelope_tree);

				std::ostringstream os;
				pt::write_xml(os, root_tree);

				utility::http::fillResponseWithHeaders(*response, os.str());
			}
		};

		//PullPoint port entrance handler
		void PullPointRequestsHandler(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request)
		{
			//osrv::auth::SECURITY_LEVELS::READ_MEDIA
			log_->Debug("Handling PullPoint request: " + request->method + " " + request->path);

			auto parsed_request = parse_pullmessages(request->content.string());

			const static std::string ACTION_PULLMESSAGESS = "http://www.onvif.org/ver10/events/wsdl/PullPointSubscription/PullMessagesRequest";
			if (parsed_request.header_action == ACTION_PULLMESSAGESS)
			{
				// NOTE: current implementation reads a timeout from the configuration and ignores a value in the request
				notifications_manager->PullMessages(response, parsed_request.header_to, EVENT_CONFIGS_TREE.get<int>("PullPoint.Timeout"),
					parsed_request.messages_limit);
			}
			
			// If there was no error, a response will be send asynchronously
			// *response << "HTTP/1.1 200 OK\r\n" << "Content-Length: 0\r\n" << "Connection: close\r\n" << "\r\n";
		}
		
		//EVENTS SERVICE PORT
		struct GetEventPropertiesHandler : public utility::http::RequestHandlerBase
		{
			GetEventPropertiesHandler() : utility::http::RequestHandlerBase("GetEventProperties", osrv::auth::SECURITY_LEVELS::READ_MEDIA)
			{
			}

			OVERLOAD_REQUEST_HANDLER
			{
				auto configs_node = EVENT_CONFIGS_TREE.get_child(GetEventProperties);

				std::string response_body;
				auto isStaticResponse = configs_node.get<bool>("ReadResponseFromFile");
				if (isStaticResponse)
				{
					auto response_filename = configs_node.get<std::string>("ResponseFilePath");
					std::ifstream event_file(CONFIGS_PATH + response_filename);
					if (!event_file.is_open())
						throw std::runtime_error("Couldn't read specified response file: " + response_filename);

					std::string read_configs((std::istreambuf_iterator<char>(event_file)),
						(std::istreambuf_iterator<char>()));
					event_file.close();

					std::swap(response_body, read_configs);
				}
				else
				{
					//TODO
					throw std::runtime_error("Not implemented yet");
				}

				utility::http::fillResponseWithHeaders(*response, response_body);
			}
		};
		
		//DEFAULT HANDLER
		void EventServiceHandler(std::shared_ptr<HttpServer::Response> response,
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

			//auto it = handlers.find(method);
			auto handler_it = std::find_if(handlers.begin(), handlers.end(),
				[&method](const utility::http::HandlerSP handler) {
					return handler->get_name() == method;
				});

			//handle requests
			if (handler_it != handlers.end())
			{
				//checking user credentials
				try
				{
					auto handler_ptr = *handler_it;
					log_->Debug("Handling EventService request: " + handler_ptr->get_name());
					
					//extract user credentials
					osrv::auth::USER_TYPE current_user = osrv::auth::USER_TYPE::ANON;
					if (server_configs->auth_scheme_ == osrv::AUTH_SCHEME::DIGEST)
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
					log_->Error(e.what());

					*response << utility::http::RESPONSE_UNAUTHORIZED << "\r\n"
						<< "Content-Type: application/soap+xml; charset=utf-8" << "\r\n"
						<< "Content-Length: " << 0 << "\r\n"
						<< utility::http::HEADER_WWW_AUTHORIZATION << ": " << digest_session->generateDigest().to_string() << "\r\n"
						<< "\r\n";
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

		void init_service(HttpServer& srv, const osrv::ServerConfigs& server_configs_instance,
			const std::string& configs_path, Logger& logger)
		{
			if (log_ != nullptr)
				return log_->Error("EventService is already inited!");

			log_ = &logger;
			log_->Debug("Initiating Event service...");

			http_server_intance = &srv;

			server_configs = &server_configs_instance;
			digest_session = server_configs_instance.digest_session_;

			CONFIGS_PATH = configs_path;

			//getting service's configs
			pt::read_json(configs_path + EVENT_CONFIGS_FILE, EVENT_CONFIGS_TREE);

			auto namespaces_tree = EVENT_CONFIGS_TREE.get_child("Namespaces");
			for (const auto& n : namespaces_tree)
				XML_NAMESPACES.insert({ n.first, n.second.get_value<std::string>() });

			notifications_manager = std::unique_ptr<osrv::event::NotificationsManager>(
				new osrv::event::NotificationsManager(logger));

			// add event generators
			auto di_event_generator = std::shared_ptr<osrv::event::IEventGenerator>(
				new event::DInputEventGenerator(3, notifications_manager->get_io_context()));
			notifications_manager->add_generator(di_event_generator);

			notifications_manager->run();

			//event service handlers
			handlers.emplace_back(new GetEventPropertiesHandler{});
			
			//PullPoint handlers
			handlers.emplace_back(new CreatePullPointSubscriptionHandler{});

			srv.resource["/onvif/event_service"]["POST"] = EventServiceHandler;

			//register a default handler for the Pullpoint requests
			//NOTE: this path pattern should be match the one generated
			//in the NotificationsManager for a new subscription
			srv.resource["/onvif/event_service/s([0-9]+)"]["POST"] = PullPointRequestsHandler;
		}

	}
}