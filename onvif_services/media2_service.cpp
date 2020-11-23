#include "media2_service.h"
#include "media_service.h" // to use some util functions

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
static const std::string GetAudioDecoderConfigurations = "GetAudioDecoderConfigurations";
static const std::string GetProfiles = "GetProfiles";
static const std::string GetVideoSourceConfigurations = "GetVideoSourceConfigurations";
static const std::string GetStreamUri = "GetStreamUri";

namespace osrv
{
    namespace media2
    {
		using handler_t = void(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request);
		static std::map<std::string, handler_t*> handlers;
		
		void GetAudioDecoderConfigurationsHandler(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request)
		{
			log_->Debug("Handle GetAudioDecoderConfigurations");

			pt::ptree ad_configs;
			auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);
			envelope_tree.add_child("s:Body.tr2:GetAudioDecoderConfigurationsResponse", ad_configs);

			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", envelope_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);

			utility::http::fillResponseWithHeaders(*response, os.str());
		}

		void GetProfilesHandler(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request)
		{
			log_->Debug("Handle GetProfiles");

			auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);

			auto profiles_configs_list = PROFILES_CONFIGS_TREE.get_child("MediaProfiles");

			// extract requested profile token (if there it is) 
			std::string profile_token;
			{
				auto request_str = request->content.string();
				std::istringstream is(request_str );
				pt::ptree xml_tree;
				pt::xml_parser::read_xml(is, xml_tree);
				profile_token = exns::find_hierarchy("Envelope.Body.GetProfiles.Token", xml_tree);
			}

			pt::ptree response_node;
			if (profile_token.empty())
			{
				// response all media profiles' configs
				for (auto elements : profiles_configs_list)
				{
					pt::ptree profile_node;
					util::profile_to_soap(elements.second, PROFILES_CONFIGS_TREE, profile_node);
					response_node.add_child("tr2:Profiles", profile_node);
				}
			}
			else
			{
				// response only one profile's configs
				auto profiles_config_it = std::find_if(profiles_configs_list.begin(),
					profiles_configs_list.end(),
					[&profile_token](const pt::ptree::value_type& i)
					{
						return i.second.get<std::string>("token") == profile_token;
					});

				if (profiles_config_it == profiles_configs_list.end())
					throw std::runtime_error("Not found a profile with token: " + profile_token);

				pt::ptree profile_node;
				util::profile_to_soap(profiles_config_it->second, PROFILES_CONFIGS_TREE, profile_node);
				response_node.add_child("tr2:Profiles", profile_node);
			}

			envelope_tree.put_child("s:Body.tr2:GetProfilesResponse", response_node);

			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", envelope_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);

			utility::http::fillResponseWithHeaders(*response, os.str());
		}
		
		void GetVideoSourceConfigurationsHandler(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request)
		{
			log_->Debug("Handle " + GetVideoSourceConfigurations);
			
			auto vs_config_list = PROFILES_CONFIGS_TREE.get_child("VideoSourceConfigurations");
			pt::ptree vs_configs_node;
			for (const auto& vs_config : vs_config_list)
			{
				pt::ptree videosource_configuration;
				osrv::media::util::fill_soap_videosource_configuration(vs_config.second, videosource_configuration);
				vs_configs_node.put_child("tr2:Configurations", videosource_configuration);
			}
			auto env_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);
			env_tree.put_child("tr2:GetVideoSourceConfigurationsResponse", vs_configs_node);
			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", env_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);

			utility::http::fillResponseWithHeaders(*response, os.str());
		}
		
		void GetStreamUriHandler(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request)
		{
			log_->Debug("Handle " + GetStreamUri);

			pt::ptree request_xml;
			pt::xml_parser::read_xml(std::istringstream{ request->content.string() }, request_xml);

			std::string requested_token;
			{
				requested_token = exns::find_hierarchy("Envelope.Body.GetStreamUri.ProfileToken", request_xml);

				log_->Debug("Requested token to get URI=" + requested_token);
			}

			auto profiles_config_list = PROFILES_CONFIGS_TREE.get_child("MediaProfiles");

			auto profile_config = std::find_if(profiles_config_list.begin(), profiles_config_list.end(),
				[requested_token](const pt::ptree::value_type& vs_obj)
				{
					return vs_obj.second.get<std::string>("token") == requested_token;
				});

			if (profile_config == profiles_config_list.end())
				throw std::runtime_error("Can't find a proper URI: the media profile does not exist. token=" + requested_token);

			auto encoder_token = profile_config->second.get<std::string>("VideoEncoderConfiguration");

			auto stream_configs_list = CONFIGS_TREE.get_child("GetStreamUri");
			auto stream_config_it = std::find_if(stream_configs_list.begin(), stream_configs_list.end(),
				[encoder_token](const pt::ptree::value_type& el)
				{return el.second.get<std::string>("VideoEncoderToken") == encoder_token; });

			if (stream_config_it == stream_configs_list.end())
				throw std::runtime_error("Could not find a stream for the requested Media Profile token=" + requested_token);

			pt::ptree response_node;
			response_node.put("tr2:Uri",
				"rtsp://127.0.0.1:8554/" + stream_config_it->second.get<std::string>("Uri"));

			auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);
			envelope_tree.put_child("s:Body.tr2:GetStreamUriResponse", response_node);

			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", envelope_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);

			utility::http::fillResponseWithHeaders(*response, os.str());
		}


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
				log_->Error("Not found an appropriate handler in Media2Service for: " + method);
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

            handlers.insert({ GetAudioDecoderConfigurations, &GetAudioDecoderConfigurationsHandler});
            handlers.insert({ GetProfiles, &GetProfilesHandler });
            handlers.insert({ GetVideoSourceConfigurations, &GetVideoSourceConfigurationsHandler });
            handlers.insert({ GetStreamUri, &GetStreamUriHandler });

            srv.resource["/onvif/media2_service"]["POST"] = Media2ServiceHandler;

        }


		namespace util
		{
			using ptree = boost::property_tree::ptree;
			void profile_to_soap(const ptree& profile_config, const ptree& configs_file, ptree& result)
			{
				result.add("<xmlattr>.token", profile_config.get<std::string>("token"));
				result.add("<xmlattr>.fixed", profile_config.get<std::string>("fixed"));
				result.add("Name", profile_config.get<std::string>("Name"));

				//Videosource
				{
					const std::string vs_token = profile_config.get<std::string>("VideoSourceConfiguration");
					auto vs_configs_list = configs_file.get_child("VideoSourceConfigurations");
					auto vs_config = std::find_if(vs_configs_list.begin(), vs_configs_list.end(),
						[vs_token](pt::ptree::value_type vs_obj)
						{
							return vs_obj.second.get<std::string>("token") == vs_token;
						});

					if (vs_config == vs_configs_list.end())
						throw std::runtime_error("Can't find VideoSourceConfiguration with token '" + vs_token + "'");

					pt::ptree videosource_configuration;
					osrv::media::util::fill_soap_videosource_configuration(vs_config->second, videosource_configuration);
					result.put_child("tr2:Configurations.tr2:VideoSource", videosource_configuration);
				}

				// videoencoder
				{
					const std::string ve_token = profile_config.get<std::string>("VideoEncoderConfiguration");
					//TODO: use the same configuartion structure with Media1  --->get_child("VideoEncoderConfigurations2")
					auto vs_configs_list = configs_file.get_child("VideoEncoderConfigurations2");
					auto vs_config = std::find_if(vs_configs_list.begin(), vs_configs_list.end(),
						[ve_token](pt::ptree::value_type vs_obj)
						{
							return vs_obj.second.get<std::string>("token") == ve_token;
						});

					if (vs_config == vs_configs_list.end())
						throw std::runtime_error("Can't find VideoEncoderConfiguration with token '" + ve_token + "'");
					pt::ptree videoencoder_configuration;
					osrv::media2::util::fill_video_encoder(vs_config->second, videoencoder_configuration);
					result.put_child("tr2:Configurations.tr2:VideoEncoder", videoencoder_configuration);
				}

			}
			
			void fill_video_encoder(const pt::ptree& config_node, pt::ptree& videoencoder_node)
			{
				videoencoder_node.add("<xmlattr>.token", config_node.get<std::string>("token"));
				videoencoder_node.add("tt:Name", config_node.get<std::string>("Name"));
				videoencoder_node.add("tt:UseCount", config_node.get<int>("UseCount"));
				videoencoder_node.add("<xmlattr>.GovLength", config_node.get<int>("GovLength"));
				videoencoder_node.add("<xmlattr>.Profile", config_node.get<std::string>("Profile"));
				videoencoder_node.add("<xmlattr>.GuaranteedFrameRate", config_node.get<bool>("GuaranteedFrameRate"));
				videoencoder_node.add("tt:Encoding", config_node.get<std::string>("Encoding"));
				videoencoder_node.add("tt:Resolution.tt:Width", config_node.get<int>("Resolution.Width"));
				videoencoder_node.add("tt:Resolution.tt:Height", config_node.get<int>("Resolution.Height"));
				videoencoder_node.add("tt:Quality", config_node.get<float>("Quality"));
			}
			
		}
    }
}
