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


static const std::string PROFILES_CONFIGS_PATH = "media_profiles.config";
static const std::string MEDIA_SERVICE_CONFIGS_PATH = "media.config";

namespace pt = boost::property_tree;
static pt::ptree CONFIGS_TREE;
static pt::ptree PROFILES_CONFIGS_TREE;
static osrv::StringsMap XML_NAMESPACES;

static std::string SERVER_ADDRESS = "http://127.0.0.1:8080/";

//the list of implemented methods
static const std::string GetProfiles = "GetProfiles";
static const std::string GetVideoSources = "GetVideoSources";

namespace osrv
{
    namespace media
    {
        using handler_t = void(std::shared_ptr<HttpServer::Response> response,
		    std::shared_ptr<HttpServer::Request> request);
		static std::map<std::string, handler_t*> handlers;

		void GetProfilesHandler(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request)
		{
			log_->Debug("Handle GetProfiles");
			
			auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);

			auto profiles_config = PROFILES_CONFIGS_TREE.get_child("MediaProfiles");
			pt::ptree response_node;
			for (auto elements : profiles_config)
			{
				pt::ptree profile_node;
				profile_node.put("trt:Profiles.<xmlattr>.token", elements.second.get<std::string>("token"));
				profile_node.put("trt:Profiles.<xmlattr>.fixed", elements.second.get<std::string>("fixed"));
				profile_node.put("trt:Profiles.tt:Name", elements.second.get<std::string>("Name"));
				
				//TODO: fill all videosource configs from the config file
				profile_node.put("trt:Profiles.tt:VideoSourceConfiguration",
					elements.second.get<std::string>("VideoSourceConfiguration"));
				
				//TODO: fill all videoencoder configs from the config file
				profile_node.put("trt:Profiles.tt:VideoEncoderConfiguration",
					elements.second.get<std::string>("VideoEncoderConfiguration"));

				response_node.insert(response_node.end(),
					profile_node.begin(),
					profile_node.end());
			}
		
			envelope_tree.put_child("s:Body.trt:GetProfilesResponse", response_node);

			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", envelope_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);

			utility::http::fillResponseWithHeaders(*response, os.str());
		}

		void GetVideoSourcesHandler(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request)
		{
			log_->Debug("Handle GetVideoSources");
			
			auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);

			auto videosources_config = CONFIGS_TREE.get_child(GetVideoSources);
			pt::ptree response_node;

			//here's Services are enumerates as array, so handle them manualy
			for (auto elements : videosources_config)
			{
				pt::ptree videosource_node;
				videosource_node.put("trt:VideoSources.<xmlattr>.token",
					elements.second.get<std::string>("token"));
				videosource_node.put("trt:VideoSources.tt:Framerate",
					elements.second.get<std::string>("Framerate"));
				videosource_node.put("trt:VideoSources.tt:Resolution.Width",
					elements.second.get<std::string>("Resolution.Width"));
				videosource_node.put("trt:VideoSources.tt:Resolution.Height",
					elements.second.get<std::string>("Resolution.Height"));

				response_node.insert(response_node.begin(),
					videosource_node.begin(), videosource_node.end());
				//response_node.add_child("trt:VideoSources", videosource_node);
			}
		
			envelope_tree.add_child("s:Body.trt:GetVideoSourcesResponse", response_node);

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

            pt::read_json(configs_path + MEDIA_SERVICE_CONFIGS_PATH, CONFIGS_TREE);
            pt::read_json(configs_path + PROFILES_CONFIGS_PATH, PROFILES_CONFIGS_TREE);

            auto namespaces_tree = CONFIGS_TREE.get_child("Namespaces");
			for (const auto& n : namespaces_tree)
				XML_NAMESPACES.insert({ n.first, n.second.get_value<std::string>() });

			handlers.insert({ GetProfiles, &GetProfilesHandler });
			handlers.insert({ GetVideoSources, &GetVideoSourcesHandler });

            srv.resource["/onvif/media_service"]["POST"] = MediaServiceHandler;
        }
    }
}