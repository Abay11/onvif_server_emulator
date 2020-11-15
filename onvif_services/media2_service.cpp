#include "media2_service.h"

#include "../Logger.hpp"
#include "../utility/XmlParser.h"
#include "../utility/HttpHelper.h"
#include "../utility/SoapHelper.h"

#include "../Simple-Web-Server/server_http.hpp"

#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

static Logger* log_ = nullptr;


static const std::string PROFILES_CONFIGS_PATH = "media_profiles.config";
static const std::string MEDIA_SERVICE_CONFIGS_PATH = "media2.config";

namespace pt = boost::property_tree;
static pt::ptree CONFIGS_TREE;
static pt::ptree PROFILES_CONFIGS_TREE;
static osrv::StringsMap XML_NAMESPACES;

static std::string SERVER_ADDRESS = "http://127.0.0.1:8080/";


//the list of implemented methods
static const std::string GetProfiles = "GetProfiles";

namespace osrv
{
    namespace media2
    {
		using handler_t = void(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request);
		static std::map<std::string, handler_t*> handlers;

        //DEFAULT HANDLER
        void Media2ServiceHandler(std::shared_ptr<HttpServer::Response> response,
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
					log_->Error("A server's error occured while processing in MediaService: " + method
						+ ". Info: " + e.what());

					*response << "HTTP/1.1 500 Server error\r\nContent-Length: " << 0 << "\r\n\r\n";
				}
			}
			else
			{
				log_->Error("Not found an appropriate handler in MediaService for: " + method);
				*response << "HTTP/1.1 400 Bad request\r\nContent-Length: " << 0 << "\r\n\r\n";
			}
        }

        void init_service(HttpServer& srv, const std::string& configs_path, Logger& logger)
        {
            if (log_ != nullptr)
                return log_->Error("Media2Service is already initiated!");

            log_ = &logger;

            log_->Debug("Initiating Media2 service...");

            pt::read_json(configs_path + MEDIA_SERVICE_CONFIGS_PATH, CONFIGS_TREE);
            pt::read_json(configs_path + PROFILES_CONFIGS_PATH, PROFILES_CONFIGS_TREE);

            auto namespaces_tree = CONFIGS_TREE.get_child("Namespaces");
            for (const auto& n : namespaces_tree)
                XML_NAMESPACES.insert({ n.first, n.second.get_value<std::string>() });

            //handlers.insert({ GetProfiles, &GetProfilesHandler });

            srv.resource["/onvif/media2_service"]["POST"] = Media2ServiceHandler;

        }
    }
}