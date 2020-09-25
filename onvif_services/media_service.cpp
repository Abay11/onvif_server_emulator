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
static const std::string GetProfile = "GetProfile";
static const std::string GetProfiles = "GetProfiles";
static const std::string GetVideoSources = "GetVideoSources";
static const std::string GetStreamUri = "GetStreamUri";

//soap helper functions
void fill_soap_media_profile(const pt::ptree& /*in_json_config*/, pt::ptree& /*out_profile_node*/,
	const std::string& /*root_node_value*/);

namespace osrv
{
    namespace media
    {
        using handler_t = void(std::shared_ptr<HttpServer::Response> response,
		    std::shared_ptr<HttpServer::Request> request);
		static std::map<std::string, handler_t*> handlers;

		void GetProfileHandler(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request)
		{
			log_->Debug("Handle GetProfile");

			pt::ptree request_xml;
			pt::xml_parser::read_xml(std::istringstream{ request->content.string() }, request_xml);

			//TODO: add way to search for child with full path like: "Envelope.Body.GetPr..."
			std::string requested_token;
			{
				auto envelope_node = exns::find("Envelope", request_xml);
				auto body_node = exns::find("Body", envelope_node->second);
				auto profile_node = exns::find("GetProfile", body_node->second);
				auto profile_token = exns::find("ProfileToken", profile_node->second);
				requested_token = profile_token->second.get_value<std::string>();
			}

			auto profiles_config_list = PROFILES_CONFIGS_TREE.get_child("MediaProfiles");

			auto profile_config = std::find_if(profiles_config_list.begin(), profiles_config_list.end(),
				[requested_token](pt::ptree::value_type vs_obj)
				{
					return vs_obj.second.get<std::string>("token") == requested_token;
				});

			if (profile_config == profiles_config_list.end())
				throw std::runtime_error("The requested profile token ProfileToken does not exist");

			pt::ptree profile_node;
			fill_soap_media_profile(profile_config->second, profile_node, "trt:Profile");

			pt::ptree response_node;
			response_node.insert(response_node.end(),
				profile_node.begin(),
				profile_node.end());
			
			auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);
			envelope_tree.put_child("s:Body.trt:GetProfileResponse", response_node);

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

			auto profiles_config = PROFILES_CONFIGS_TREE.get_child("MediaProfiles");
			pt::ptree response_node;
			profiles_config.begin();
			for (auto elements : profiles_config)
			{
				pt::ptree profile_node;
				fill_soap_media_profile(elements.second, profile_node, "trt:Profiles");

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
		
		void GetStreamUriHandler(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request)
		{
			log_->Debug("Handle GetStreamUri");

			pt::ptree request_xml;
			pt::xml_parser::read_xml(std::istringstream{ request->content.string() }, request_xml);

			//TODO: add way to search for child with full path like: "Envelope.Body.GetPr..."
			std::string requested_token;
			{
				auto envelope_node = exns::find("Envelope", request_xml);
				auto body_node = exns::find("Body", envelope_node->second);
				auto profile_node = exns::find("GetStreamUri", body_node->second);
				auto profile_token = exns::find("ProfileToken", profile_node->second);
				requested_token = profile_token->second.get_value<std::string>();

				log_->Debug("Requested token to get URI: " + requested_token);
			}

			auto profiles_config_list = PROFILES_CONFIGS_TREE.get_child("MediaProfiles");

			auto profile_config = std::find_if(profiles_config_list.begin(), profiles_config_list.end(),
				[requested_token](pt::ptree::value_type vs_obj)
				{
					return vs_obj.second.get<std::string>("token") == requested_token;
				});

			if (profile_config == profiles_config_list.end())
				throw std::runtime_error("The media profile does not exist.");
			
			auto encoder_token = profile_config->second.get<std::string>("VideoEncoderConfiguration");

			auto stream_configs_list = CONFIGS_TREE.get_child("GetStreamUri");
			auto stream_config_it = std::find_if(stream_configs_list.begin(), stream_configs_list.end(),
				[encoder_token](const pt::ptree::value_type& el)
				{return el.second.get<std::string>("VideoEncoderToken") == encoder_token; });

			if (stream_config_it == stream_configs_list.end())
				throw std::runtime_error("Could not find a stream for the requested Media Profile.");

			pt::ptree response_node;
			response_node.put("trt:MediaUri.tt:Uri",
				"rtsp://127.0.0.1:554/" + stream_config_it->second.get<std::string>("Uri"));
			response_node.put("trt:MediaUri.tt:InvalidAfterConnect",
				stream_config_it->second.get<std::string>("InvalidAfterConnect"));
			response_node.put("trt:MediaUri.tt:InvalidAfterReboot",
				stream_config_it->second.get<std::string>("InvalidAfterReboot"));
			response_node.put("trt:MediaUri.tt:Timeout",
				stream_config_it->second.get<std::string>("Timeout"));

			auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);
			envelope_tree.put_child("s:Body.trt:GetStreamUriResponse", response_node);

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

			handlers.insert({ GetProfile, &GetProfileHandler });
			handlers.insert({ GetProfiles, &GetProfilesHandler });
			handlers.insert({ GetVideoSources, &GetVideoSourcesHandler });
			handlers.insert({ GetStreamUri, &GetStreamUriHandler });

            srv.resource["/onvif/media_service"]["POST"] = MediaServiceHandler;
        }
    }
}

void fill_soap_media_profile(const pt::ptree& json_config, pt::ptree& profile_node,
	const std::string& root_node_value)
{
	profile_node.put(root_node_value + ".<xmlattr>.token", json_config.get<std::string>("token"));
	profile_node.put(root_node_value + ".<xmlattr>.fixed", json_config.get<std::string>("fixed"));
	profile_node.put(root_node_value + ".tt:Name", json_config.get<std::string>("Name"));

	//Videosource
	{
		const std::string vs_token = json_config.get<std::string>("VideoSourceConfiguration");
		auto vs_configs_list = PROFILES_CONFIGS_TREE.get_child("VideoSourceConfigurations");
		auto vs_config = std::find_if(vs_configs_list.begin(), vs_configs_list.end(),
			[vs_token](pt::ptree::value_type vs_obj)
			{
				return vs_obj.second.get<std::string>("token") == vs_token;
			});

		if (vs_config == vs_configs_list.end())
			throw std::runtime_error("Can't find VideoSourceConfiguration with token '" + vs_token + "'");

		profile_node.put(root_node_value + ".tt:VideoSourceConfiguration.<xmlattr>.token",
			vs_config->second.get<std::string>("token"));
		profile_node.put(root_node_value + ".tt:VideoSourceConfiguration.tt:Name",
			vs_config->second.get<std::string>("Name"));
		profile_node.put(root_node_value + ".tt:VideoSourceConfiguration.tt:UseCount",
			vs_config->second.get<std::string>("UseCount"));
		profile_node.put(root_node_value + ".tt:VideoSourceConfiguration.<xmlattr>.ViewMode",
			vs_config->second.get<std::string>("ViewMode"));
		profile_node.put(root_node_value + ".tt:VideoSourceConfiguration.tt:SourceToken",
			vs_config->second.get<std::string>("SourceToken"));
		profile_node.put(root_node_value + ".tt:VideoSourceConfiguration.tt:Bounds.<xmlattr>.x",
			vs_config->second.get<std::string>("Bounds.x"));
		profile_node.put(root_node_value + ".tt:VideoSourceConfiguration.tt:Bounds.<xmlattr>.y",
			vs_config->second.get<std::string>("Bounds.y"));
		profile_node.put(root_node_value + ".tt:VideoSourceConfiguration.tt:Bounds.<xmlattr>.width",
			vs_config->second.get<std::string>("Bounds.width"));
		profile_node.put(root_node_value + ".tt:VideoSourceConfiguration.tt:Bounds.<xmlattr>.height",
			vs_config->second.get<std::string>("Bounds.height"));
	}

	//VideoEncoder
	{
		auto ve_configs_list = PROFILES_CONFIGS_TREE.get_child("VideoEncoderConfigurations");
		std::string ve_token = json_config.get<std::string>("VideoEncoderConfiguration");
		auto ve_config = std::find_if(ve_configs_list.begin(), ve_configs_list.end(),
			[ve_token](pt::ptree::value_type ve_obj)
			{
				return ve_obj.second.get<std::string>("token") == ve_token;
			});

		if (ve_config == ve_configs_list.end())
			throw std::runtime_error("Can't find VideoEncoderConfiguration with token '" + ve_token + "'");

		profile_node.put(root_node_value + ".tt:VideoEncoderConfiguration.<xmlattr>.token",
			ve_config->second.get<std::string>("token"));
		profile_node.put(root_node_value + ".tt:VideoEncoderConfiguration.tt:Name",
			ve_config->second.get<std::string>("Name"));
		profile_node.put(root_node_value + ".tt:VideoEncoderConfiguration.tt:UseCount",
			ve_config->second.get<std::string>("UseCount"));
		profile_node.put(root_node_value + ".tt:VideoEncoderConfiguration.tt:Encoding",
			ve_config->second.get<std::string>("Encoding"));
		profile_node.put(root_node_value + ".tt:VideoEncoderConfiguration.tt:Resolution.tt:Width",
			ve_config->second.get<std::string>("Resolution.Width"));
		profile_node.put(root_node_value + ".tt:VideoEncoderConfiguration.tt:Resolution.tt:Height",
			ve_config->second.get<std::string>("Resolution.Height"));
		profile_node.put(root_node_value + ".tt:VideoEncoderConfiguration.tt:Quality",
			ve_config->second.get<std::string>("Quality"));

		//ratecontrol is optional
		auto ratecontrol_config_it = ve_config->second.find("RateControl");
		if (ratecontrol_config_it != ve_config->second.not_found())
		{
			profile_node.put(root_node_value + ".tt:VideoEncoderConfiguration.tt:RateControl.<xmlattr>.GuaranteedFrameRate",
				ratecontrol_config_it->second.get<std::string>("GuaranteedFrameRate"));
			profile_node.put(root_node_value + ".tt:VideoEncoderConfiguration.tt:RateControl.tt:FrameRateLimit",
				ratecontrol_config_it->second.get<std::string>("FrameRateLimit"));
			profile_node.put(root_node_value + ".tt:VideoEncoderConfiguration.tt:RateControl.tt:EncodingInterval",
				ratecontrol_config_it->second.get<std::string>("EncodingInterval"));
			profile_node.put(root_node_value + ".tt:VideoEncoderConfiguration.tt:RateControl.tt:BitrateLimit",
				ratecontrol_config_it->second.get<std::string>("BitrateLimit"));
		}

		const auto& codec = ve_config->second.get<std::string>("Encoding");
		if ("H264" == codec)
		{
			//codecs info is optional
			auto h264_config_it = ve_config->second.find("H264");
			if (h264_config_it != ve_config->second.not_found())
			{
				profile_node.put(root_node_value + ".tt:VideoEncoderConfiguration.tt:H264.tt:GovLength",
					h264_config_it->second.get<std::string>("GovLength"));
				profile_node.put(root_node_value + ".tt:VideoEncoderConfiguration.tt:H264.tt:H264Profile",
					h264_config_it->second.get<std::string>("H264Profile"));
			}
		}
		else if ("MPEG4" == codec)
		{
			//TODO
		}

		//Multicast
		profile_node.put(root_node_value + ".tt:VideoEncoderConfiguration.tt:Multicast.tt:Address.tt:Type",
			ve_config->second.get<std::string>("Multicast.Address.Type"));
		profile_node.put(root_node_value + ".tt:VideoEncoderConfiguration.tt:Multicast.tt:Address.tt:IPv4Address",
			ve_config->second.get<std::string>("Multicast.Address.IPv4Address"));
		profile_node.put(root_node_value + ".tt:VideoEncoderConfiguration.tt:Multicast.tt:Port",
			ve_config->second.get<std::string>("Multicast.Port"));
		profile_node.put(root_node_value + ".tt:VideoEncoderConfiguration.tt:Multicast.tt:TTL",
			ve_config->second.get<std::string>("Multicast.TTL"));
		profile_node.put(root_node_value + ".tt:VideoEncoderConfiguration.tt:Multicast.tt:AutoStart",
			ve_config->second.get<std::string>("Multicast.AutoStart"));

		profile_node.put(root_node_value + ".tt:VideoEncoderConfiguration.tt:SessionTimeout",
			ve_config->second.get<std::string>("SessionTimeout"));
	}
}
