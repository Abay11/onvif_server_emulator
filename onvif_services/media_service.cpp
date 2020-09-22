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
				
				//Videosource
				{
					const std::string vs_token = elements.second.get<std::string>("VideoSourceConfiguration");
					auto vs_configs_list = PROFILES_CONFIGS_TREE.get_child("VideoSourceConfigurations");
					auto vs_config = std::find_if(vs_configs_list.begin(), vs_configs_list.end(),
						[vs_token](pt::ptree::value_type vs_obj)
						{
							return vs_obj.second.get<std::string>("token") == vs_token;
						});

					if (vs_config == vs_configs_list.end())
						throw std::runtime_error("Can't find VideoSourceConfiguration with token '" + vs_token + "'");

					profile_node.put("trt:Profiles.tt:VideoSourceConfiguration.<xmlattr>.token",
						vs_config->second.get<std::string>("token"));
					profile_node.put("trt:Profiles.tt:VideoSourceConfiguration.tt:Name",
						vs_config->second.get<std::string>("Name"));
					profile_node.put("trt:Profiles.tt:VideoSourceConfiguration.tt:UseCount",
						vs_config->second.get<std::string>("UseCount"));
					profile_node.put("trt:Profiles.tt:VideoSourceConfiguration.<xmlattr>.ViewMode",
						vs_config->second.get<std::string>("ViewMode"));
					profile_node.put("trt:Profiles.tt:VideoSourceConfiguration.tt:SourceToken",
						vs_config->second.get<std::string>("SourceToken"));
					profile_node.put("trt:Profiles.tt:VideoSourceConfiguration.tt:Bounds.<xmlattr>.x",
						vs_config->second.get<std::string>("Bounds.x"));
					profile_node.put("trt:Profiles.tt:VideoSourceConfiguration.tt:Bounds.<xmlattr>.y",
						vs_config->second.get<std::string>("Bounds.y"));
					profile_node.put("trt:Profiles.tt:VideoSourceConfiguration.tt:Bounds.<xmlattr>.width",
						vs_config->second.get<std::string>("Bounds.width"));
					profile_node.put("trt:Profiles.tt:VideoSourceConfiguration.tt:Bounds.<xmlattr>.height",
						vs_config->second.get<std::string>("Bounds.height"));
				}
				
				//VideoEncoder
				{
					auto ve_configs_list = PROFILES_CONFIGS_TREE.get_child("VideoEncoderConfigurations");
					std::string ve_token = elements.second.get<std::string>("VideoEncoderConfiguration");
					auto ve_config = std::find_if(ve_configs_list.begin(), ve_configs_list.end(),
						[ve_token](pt::ptree::value_type ve_obj)
						{
							return ve_obj.second.get<std::string>("token") == ve_token;
						});

					if (ve_config == ve_configs_list.end())
						throw std::runtime_error("Can't find VideoEncoderConfiguration with token '" + ve_token + "'");

					profile_node.put("trt:Profiles.tt:VideoEncoderConfiguration.<xmlattr>.token",
						ve_config->second.get<std::string>("token"));
					profile_node.put("trt:Profiles.tt:VideoEncoderConfiguration.tt:Name",
						ve_config->second.get<std::string>("Name"));
					profile_node.put("trt:Profiles.tt:VideoEncoderConfiguration.tt:UseCount",
						ve_config->second.get<std::string>("UseCount"));
					profile_node.put("trt:Profiles.tt:VideoEncoderConfiguration.tt:Encoding",
						ve_config->second.get<std::string>("Encoding"));
					profile_node.put("trt:Profiles.tt:VideoEncoderConfiguration.tt:Resolution.tt:Width",
						ve_config->second.get<std::string>("Resolution.Width"));
					profile_node.put("trt:Profiles.tt:VideoEncoderConfiguration.tt:Resolution.tt:Height",
						ve_config->second.get<std::string>("Resolution.Height"));
					profile_node.put("trt:Profiles.tt:VideoEncoderConfiguration.tt:Quality",
						ve_config->second.get<std::string>("Quality"));
					
					//ratecontrol is optional
					auto ratecontrol_config_it = ve_config->second.find("RateControl");
					if(ratecontrol_config_it != ve_config->second.not_found())
					{
						profile_node.put("trt:Profiles.tt:VideoEncoderConfiguration.tt:RateControl.<xmlattr>.GuaranteedFrameRate",
							ratecontrol_config_it->second.get<std::string>("GuaranteedFrameRate"));
						profile_node.put("trt:Profiles.tt:VideoEncoderConfiguration.tt:RateControl.tt:FrameRateLimit",
							ratecontrol_config_it->second.get<std::string>("FrameRateLimit"));
						profile_node.put("trt:Profiles.tt:VideoEncoderConfiguration.tt:RateControl.tt:EncodingInterval",
							ratecontrol_config_it->second.get<std::string>("EncodingInterval"));
						profile_node.put("trt:Profiles.tt:VideoEncoderConfiguration.tt:RateControl.tt:BitrateLimit",
							ratecontrol_config_it->second.get<std::string>("BitrateLimit"));
					}

					const auto& codec = ve_config->second.get<std::string>("Encoding");
					if ("H264" == codec)
					{
						//codecs info is optional
						auto h264_config_it = ve_config->second.find("H264");
						if (h264_config_it != ve_config->second.not_found())
						{
							profile_node.put("trt:Profiles.tt:VideoEncoderConfiguration.tt:H264.tt:GovLength",
								h264_config_it->second.get<std::string>("GovLength"));
							profile_node.put("trt:Profiles.tt:VideoEncoderConfiguration.tt:H264.tt:H264Profile",
								h264_config_it->second.get<std::string>("H264Profile"));
						}
					}
					else if ("MPEG4" == codec)
					{
						//TODO
					}

					//Multicast
					profile_node.put("trt:Profiles.tt:VideoEncoderConfiguration.tt:Multicast.tt:Address.tt:Type",
						ve_config->second.get<std::string>("Multicast.Address.Type"));
					profile_node.put("trt:Profiles.tt:VideoEncoderConfiguration.tt:Multicast.tt:Address.tt:IPv4Address",
						ve_config->second.get<std::string>("Multicast.Address.IPv4Address"));
					profile_node.put("trt:Profiles.tt:VideoEncoderConfiguration.tt:Multicast.tt:Port",
						ve_config->second.get<std::string>("Multicast.Port"));
					profile_node.put("trt:Profiles.tt:VideoEncoderConfiguration.tt:Multicast.tt:TTL",
						ve_config->second.get<std::string>("Multicast.TTL"));
					profile_node.put("trt:Profiles.tt:VideoEncoderConfiguration.tt:Multicast.tt:AutoStart",
						ve_config->second.get<std::string>("Multicast.AutoStart"));

					profile_node.put("trt:Profiles.tt:VideoEncoderConfiguration.tt:SessionTimeout",
						ve_config->second.get<std::string>("SessionTimeout"));
				}

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