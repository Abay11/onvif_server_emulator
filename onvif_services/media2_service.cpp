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
static const std::string GetProfiles = "GetProfiles";

namespace osrv
{
    namespace media2
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
				util::profile_to_soap(elements.second, PROFILES_CONFIGS_TREE, profile_node);
				response_node.add_child("tr2:Profiles", profile_node);
			}

			envelope_tree.put_child("s:Body.tr2:GetProfilesResponse", response_node);

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

            handlers.insert({ GetProfiles, &GetProfilesHandler });

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
