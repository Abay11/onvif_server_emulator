#include "media_service.h"

#include "../Logger.hpp"
#include "../utility/XmlParser.h"
#include "../utility/HttpHelper.h"
#include "../utility/SoapHelper.h"

#include "../Simple-Web-Server/server_http.hpp"

#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

static Logger* log_ = nullptr;

namespace pt = boost::property_tree;
static pt::ptree CONFIGS_TREE;
static osrv::StringsMap XML_NAMESPACES;

static std::string SERVER_ADDRESS = "http://127.0.0.1:8080/";

//the list of implemented methods
static const std::string GetVideoSources = "GetVideoSources";

namespace osrv
{
    namespace media
    {
        using handler_t = void(std::shared_ptr<HttpServer::Response> response,
		    std::shared_ptr<HttpServer::Request> request);
		static std::map<std::string, handler_t*> handlers;

		void GetVideoSourcesHandler(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request)
		{
			log_->Debug("Handle GetVideoSources");
			
			auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);

			auto videosources_config = CONFIGS_TREE.get_child(GetVideoSources);
			pt::ptree videosources_nodes;

			//here's Services are enumerates as array, so handle them manualy
			for (auto elements : videosources_config)
			{
				pt::ptree videosource_node;
				videosource_node.put("trt:token", elements.second.get<std::string>("token"));
				videosource_node.put("tt:Framerate", elements.second.get<std::string>("Framerate"));
				videosource_node.put("tt:Resolution.Width", elements.second.get<std::string>("Resolution.Width"));
				videosource_node.put("tt:Resolution.Height", elements.second.get<std::string>("Resolution.Height"));

				videosources_nodes.add_child("trt:VideoSources", videosource_node);
			}
		
			envelope_tree.add_child("s:Body.trt:GetVideoSourcesResponse", videosources_nodes);

			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", envelope_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);

			utility::http::fillResponseWithHeaders(*response, os.str());
		}

		//DEFAULT HANDLER
		void MediaServiceHandler(std::shared_ptr<HttpServer::Response> response,
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
            if(log_ != nullptr)
                return log_->Error("MediaService is already inited!");
            
            log_ = &logger;
            
			log_->Debug("Initiating Media service...");

            pt::read_json(configs_path, CONFIGS_TREE);

            auto namespaces_tree = CONFIGS_TREE.get_child("Namespaces");
			for (const auto& n : namespaces_tree)
				XML_NAMESPACES.insert({ n.first, n.second.get_value<std::string>() });

			handlers.insert({ GetVideoSources, &GetVideoSourcesHandler });

            srv.resource["/onvif/media_service"]["POST"] = MediaServiceHandler;
        }
    }
}