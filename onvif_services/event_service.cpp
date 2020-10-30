#include "event_service.h"

#include "../Logger.hpp"
#include "../Server.h"
#include "../utility/XmlParser.h"
#include "../utility/HttpHelper.h"

#include "../Simple-Web-Server/server_http.hpp"

#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <map>

static Logger* log_ = nullptr;

static const osrv::ServerConfigs* server_configs;
static DigestSessionSP digest_session;

namespace pt = boost::property_tree;
static pt::ptree EVENT_CONFIGS_TREE;

static std::string CONFIGS_PATH; //will be init with the service initialization
static const std::string EVENT_CONFIGS_FILE = "event.config";

//List of implemented methods of Events service port
const std::string GetEventProperties = "GetEventProperties";

namespace osrv
{
	namespace event
	{
		static std::vector<utility::http::HandlerSP> handlers;

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
					log_->Debug("Handling DeviceService request: " + handler_ptr->get_name());
					
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

		void init_service(HttpServer& srv, const osrv::ServerConfigs& server_configs_instance, const std::string& configs_path, Logger& logger)
		{
			if (log_ != nullptr)
				return log_->Error("EventService is already inited!");

			log_ = &logger;
			log_->Debug("Initiating Event service...");

			server_configs = &server_configs_instance;
			digest_session = server_configs_instance.digest_session_;

			CONFIGS_PATH = configs_path;

			//getting service's configs
			pt::read_json(configs_path + EVENT_CONFIGS_FILE, EVENT_CONFIGS_TREE);

			//event service handlers
			handlers.emplace_back(new GetEventPropertiesHandler{});

			srv.resource["/onvif/event_service"]["POST"] = EventServiceHandler;
		}

	}
}