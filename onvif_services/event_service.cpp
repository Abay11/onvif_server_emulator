#include "event_service.h"

#include "../Logger.hpp"
#include "../utility/XmlParser.h"
#include "../utility/HttpHelper.h"

#include "../Simple-Web-Server/server_http.hpp"

#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <map>

static Logger* log_ = nullptr;

namespace pt = boost::property_tree;
static pt::ptree CONFIGS_TREE;

static std::string CONFIGS_PATH; //will be init with the service initialization
static const std::string EVENT_CONFIGS_FILE = "event.config";

//List of implemented methods of Events service port
const std::string GetEventProperties = "GetEventProperties";

namespace osrv
{
	namespace event
	{
		using handler_t = void(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request);
		static std::map<std::string, handler_t*> handlers;

		//EVENTS SERVICE PORT
		void GetEventPropertiesHandler(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request)
		{
			log_->Debug("Handle GetEventProperties");

			auto configs_node = CONFIGS_TREE.get_child(GetEventProperties);

			std::string response_body;
			auto isStaticResponse = configs_node.get<bool>("ReadResponseFromFile");
			if(isStaticResponse)
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
				return log_->Error("EventService is already inited!");

			log_ = &logger;

			log_->Debug("Initiating Event service...");

			CONFIGS_PATH = configs_path;

			//getting service's configs
			pt::read_json(configs_path + EVENT_CONFIGS_FILE, CONFIGS_TREE);

			//event service handlers
			handlers.insert({ GetEventProperties, &GetEventPropertiesHandler });

			srv.resource["/onvif/event_service"]["POST"] = EventServiceHandler;
		}

	}
}