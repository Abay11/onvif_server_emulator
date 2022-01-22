#include "media_service.h"

#include "../onvif/OnvifRequest.h"

#include "../Logger.h"
#include "../utility/XmlParser.h"
#include "../utility/HttpHelper.h"
#include "../utility/SoapHelper.h"
#include "../utility/AudioSourceReader.h"
#include "../Server.h"

#include "../Simple-Web-Server/server_http.hpp"

#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <algorithm>

namespace pt = boost::property_tree;

//the list of implemented methods
static const std::string GetAudioDecoderConfigurations = "GetAudioDecoderConfigurations";
static const std::string GetAudioEncoderConfigurationOptions = "GetAudioEncoderConfigurationOptions";
static const std::string GetAudioEncoderConfiguration = "GetAudioEncoderConfiguration";
static const std::string GetAudioOutputs = "GetAudioOutputs";
static const std::string GetAudioSourceConfigurations = "GetAudioSourceConfigurations";
static const std::string GetAudioSources = "GetAudioSources";
static const std::string GetCompatibleAudioSourceConfigurations = "GetCompatibleAudioSourceConfigurations";
static const std::string GetProfile = "GetProfile";
static const std::string GetProfiles = "GetProfiles";
static const std::string GetVideoAnalyticsConfigurations = "GetVideoAnalyticsConfigurations";
static const std::string GetVideoSourceConfiguration = "GetVideoSourceConfiguration";
static const std::string GetVideoSourceConfigurations = "GetVideoSourceConfigurations";
static const std::string GetVideoSources = "GetVideoSources";
static const std::string GetStreamUri = "GetStreamUri";

//soap helper functions
void fill_soap_media_profile(const pt::ptree& /*in_json_config*/, pt::ptree& /*out_profile_node*/,
	const std::string& /*root_node_value*/, const pt::ptree& /*profiles_cfg*/);

namespace osrv
{
	struct GetAudioDecoderConfigurationsHandler : public OnvifRequestBase
	{
		GetAudioDecoderConfigurationsHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs)
			: OnvifRequestBase(GetAudioDecoderConfigurations, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs)
		{
		}

		void operator()(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request) override
		{
			pt::ptree ad_configs;
			auto envelope_tree = utility::soap::getEnvelopeTree(ns_);
			envelope_tree.add_child("s:Body.trt:GetAudioDecoderConfigurationsResponse", ad_configs);

			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", envelope_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);

			utility::http::fillResponseWithHeaders(*response, os.str());
		}
	};
	
	struct GetAudioEncoderConfigurationOptionsHandler : public OnvifRequestBase
	{
		GetAudioEncoderConfigurationOptionsHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs,
			const std::shared_ptr<pt::ptree>& profiles_configs)
			: OnvifRequestBase(GetAudioEncoderConfigurationOptions, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs)
			, profiles_configs_(profiles_configs)
		{
		}

		void operator()(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request) override
		{
			pt::ptree options_tree;

			auto fillAEOptions = [](const pt::ptree& in, pt::ptree& out) {
				// NOTE: IN MEDIA1 there is only G711 and AAC encoding
				// so we need correct name here for PCMU and MP4A-LATM.
				// PCMA I just skip
				auto encoding = in.get<std::string>("Encoding");
				if (encoding == "PCMA") return;
				if (encoding == "PCMU") encoding = "G711";
				if (encoding == "MP4A-LATM") encoding = "AAC";

				out.add("tt:Encoding", encoding);

				for (auto i : in.get_child("BitrateList"))
					out.add("tt:BitrateList.tt:Items", i.second.get_value<int>());

				for (auto i : in.get_child("SampleRateList"))
					out.add("tt:SampleRateList.tt:Items", i.second.get_value<int>());
			};

			// TODO: hardcoded value
			auto options = profiles_configs_->get_child("AudioEncoderConfigurationOptions");
			for (const auto& o : options)
			{
				pt::ptree options_node;
				fillAEOptions(o.second, options_node);
				options_tree.add_child("tt:Options", options_node);
			}


			auto envelope_tree = utility::soap::getEnvelopeTree(ns_);
			envelope_tree.add_child("s:Body.trt:GetAudioEncoderConfigurationOptionsResponse.trt:Options", options_tree);

			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", envelope_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);

			utility::http::fillResponseWithHeaders(*response, os.str());
		}

	private:
		const std::shared_ptr<pt::ptree>& profiles_configs_;
	};

	struct GetAudioEncoderConfigurationHandler : public OnvifRequestBase
	{
		GetAudioEncoderConfigurationHandler(const std::map<std::string, std::string>& xs,
			const std::shared_ptr<pt::ptree>& configs,
			const std::shared_ptr<pt::ptree>& profiles_configs)
			: OnvifRequestBase(GetAudioEncoderConfiguration, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs)
			, profiles_configs_(profiles_configs)
		{
		}

		void operator()(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request) override
		{
			pt::ptree request_xml_tree;
			pt::xml_parser::read_xml(request->content, request_xml_tree);
			auto requested_config_token = exns::find_hierarchy("GetAudioEncoderConfiguration.ConfigurationToken", request_xml_tree);

			// TODO: REMOVE IT
			if (requested_config_token.empty())
				requested_config_token = "AudioEncCfg0";

			auto aeCfg = utility::AudioEncoderReaderByToken(requested_config_token, *profiles_configs_).AudioEncoder();

			pt::ptree ae_node;
			media::util::fillAEConfig(aeCfg, ae_node);
			
			auto envelope_tree = utility::soap::getEnvelopeTree(ns_);
			envelope_tree.add_child("s:Body.trt:GetAudioEncoderConfigurationResponse.trt:Configuration", ae_node);

			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", envelope_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);

			utility::http::fillResponseWithHeaders(*response, os.str());
		}
	
	private:
		const std::shared_ptr<pt::ptree>& profiles_configs_;
	};

	struct GetAudioOutputsHandler : public OnvifRequestBase
	{
		GetAudioOutputsHandler(const std::map<std::string, std::string>& xs,
			const std::shared_ptr<pt::ptree>& configs)
			: OnvifRequestBase(GetAudioOutputs, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs)
		{
		}

		void operator()(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request) override
		{
			pt::ptree aoutputs;
			auto envelope_tree = utility::soap::getEnvelopeTree(ns_);
			envelope_tree.add_child("s:Body.trt:GetAudioOutputsResponse", aoutputs);

			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", envelope_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);

			utility::http::fillResponseWithHeaders(*response, os.str());
		}
	};

	struct GetAudioSourceConfigurationsHandler : public OnvifRequestBase
	{
		GetAudioSourceConfigurationsHandler(const std::map<std::string, std::string>& xs,
			const std::shared_ptr<pt::ptree>& configs)
			: OnvifRequestBase(GetAudioSourceConfigurations, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs)
		{
		}

		void operator()(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request) override
		{
			pt::ptree as_configs;
			auto envelope_tree = utility::soap::getEnvelopeTree(ns_);
			envelope_tree.add_child("s:Body.trt:GetAudioSourceConfigurationsResponse", as_configs);

			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", envelope_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);

			utility::http::fillResponseWithHeaders(*response, os.str());
		}
	};

	struct GetAudioSourcesHandler : public OnvifRequestBase
	{
		GetAudioSourcesHandler(const std::map<std::string, std::string>& xs,
			const std::shared_ptr<pt::ptree>& configs,
			const std::shared_ptr<pt::ptree>& profiles_configs)
			: OnvifRequestBase(GetAudioSources, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs)
			, profiles_configs_(profiles_configs)
		{
		}

		void operator()(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request) override
		{
			pt::ptree asources;

			pt::ptree source_tree;
			auto a = utility::AudioSourceConfigsReader("AudioSrcCfg0", *profiles_configs_).AudioSource(); // TODO: hardcoded value
			source_tree.add("<xmlattr>.token", a.get<std::string>("token"));
			source_tree.add("trt:Channels", 1); //TODO: hardcoded value
			asources.add_child("trt:AudioSources", source_tree);

			auto envelope_tree = utility::soap::getEnvelopeTree(ns_);
			envelope_tree.add_child("s:Body.trt:GetAudioSourcesResponse", asources);

			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", envelope_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);

			utility::http::fillResponseWithHeaders(*response, os.str());
		}

	private:
		const std::shared_ptr<pt::ptree>& profiles_configs_;
	};

	struct GetCompatibleAudioSourceConfigurationsHandler : public OnvifRequestBase
	{
		GetCompatibleAudioSourceConfigurationsHandler(const std::map<std::string, std::string>& xs,
			const std::shared_ptr<pt::ptree>& configs,
			const std::shared_ptr<pt::ptree>& profiles_configs)
			: OnvifRequestBase(GetCompatibleAudioSourceConfigurations, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs)
			, profiles_configs_(profiles_configs)
		{
		}

		void operator()(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request) override
		{
			pt::ptree asources;

			//pt::ptree source_tree;
			//auto a = utility::AudioSourceConfigsReader("AudioSrcCfg0", *profiles_configs_).AudioSource(); // TODO: hardcoded value
			//source_tree.add("<xmlattr>.token", a.get<std::string>("token"));
			//source_tree.add("trt:Channels", 1); //TODO: hardcoded value
			//asources.add_child("trt:AudioSource", source_tree);

			auto envelope_tree = utility::soap::getEnvelopeTree(ns_);
			envelope_tree.add_child("s:Body.trt:GetCompatibleAudioSourceConfigurationsResponse", asources);

			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", envelope_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);

			utility::http::fillResponseWithHeaders(*response, os.str());
		}

	private:
		const std::shared_ptr<pt::ptree>& profiles_configs_;
	};

	struct GetProfileHandler : public OnvifRequestBase
	{
		GetProfileHandler(const std::map<std::string, std::string>& xs,
			const std::shared_ptr<pt::ptree>& configs,
			osrv::ServerConfigs& server_cfg, const std::shared_ptr<pt::ptree>& profiles_cfg)
			: OnvifRequestBase(GetProfile, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs)
			, server_cfg_(server_cfg)
			, profiles_cfg_(profiles_cfg)
		{
		}

		void operator()(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request) override
		{
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

			std::string cleaned_token = media::util::MultichannelProfilesNamesConverter(requested_token).CleanedName();

			auto profiles_config_list = profiles_cfg_->get_child("MediaProfiles");
			auto profile_config = std::find_if(profiles_config_list.begin(), profiles_config_list.end(),
				[cleaned_token](pt::ptree::value_type vs_obj)
				{
					return vs_obj.second.get<std::string>("token") == cleaned_token;
				});

			if (profile_config == profiles_config_list.end())
				throw std::runtime_error("The requested profile token ProfileToken does not exist");

			pt::ptree profile_node;
			fill_soap_media_profile(profile_config->second, profile_node, "trt:Profile", *profiles_cfg_);

			if (server_cfg_.multichannel_enabled_)
			{
				profile_node.put("<xmlattr>.token", requested_token);
			}

			auto envelope_tree = utility::soap::getEnvelopeTree(ns_);
			envelope_tree.put_child("s:Body.trt:GetProfileResponse.trt:Profile", profile_node);

			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", envelope_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);

			utility::http::fillResponseWithHeaders(*response, os.str());
		}

	private:
		const osrv::ServerConfigs& server_cfg_;
		const std::shared_ptr<pt::ptree>& profiles_cfg_;
	};

	struct GetProfilesHandler : public OnvifRequestBase
	{
		GetProfilesHandler(const std::map<std::string, std::string>& xs,
			const std::shared_ptr<pt::ptree>& configs,
			osrv::ServerConfigs& server_cfg, const std::shared_ptr<pt::ptree>& profiles_cfg)
			: OnvifRequestBase(GetProfiles, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs)
			, server_cfg_(server_cfg)
			, profiles_cfg_(profiles_cfg)
		{
		}

		void operator()(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request) override
		{
			auto envelope_tree = utility::soap::getEnvelopeTree(ns_);

			auto profiles_config = profiles_cfg_->get_child("MediaProfiles");
			pt::ptree response_node;

			if (server_cfg_.multichannel_enabled_)
			{
				std::string vsToken = profiles_config.front().second.get<std::string>("VideoSourceConfiguration");
				for (size_t i = 0; i < server_cfg_.channels_count_; ++i)
				{
					auto it = profiles_config.begin();
					while (std::find_if(it, profiles_config.end(),
						[vsToken, this](pt::ptree::value_type tree)
						{
							return tree.second.get<std::string>("VideoSourceConfiguration") == vsToken;
						}) != profiles_config.end())
					{
						pt::ptree profile_node;
						fill_soap_media_profile(it->second, profile_node, "", *profiles_cfg_);

						std::string profileToken = std::to_string(i) + "_" 
							+ it->second.get<std::string>("token");
						profile_node.put("<xmlattr>.token", profileToken);

						std::string vsToken = "VideoSource" + std::to_string(i);
						profile_node.put("tt:VideoSourceConfiguration.tt:SourceToken", vsToken);

						response_node.add_child("trt:Profiles", profile_node);
						++it;
					}
				}

			}
			else
			{
				for (auto elements : profiles_config)
				{
					pt::ptree profile_node;
					fill_soap_media_profile(elements.second, profile_node, "", *profiles_cfg_);

					response_node.add_child("trt:Profiles", profile_node);
				}
			}

			envelope_tree.put_child("s:Body.trt:GetProfilesResponse", response_node);

			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", envelope_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);

			utility::http::fillResponseWithHeaders(*response, os.str());
		}

	private:
		const osrv::ServerConfigs& server_cfg_;
		const std::shared_ptr<pt::ptree>& profiles_cfg_;
	};

	struct GetVideoAnalyticsConfigurationsHandler : public OnvifRequestBase
	{
		GetVideoAnalyticsConfigurationsHandler(const std::map<std::string, std::string>& xs,
			const std::shared_ptr<pt::ptree>& configs)
			: OnvifRequestBase(GetVideoAnalyticsConfigurations, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs)
		{
		}

		void operator()(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request) override
		{
			pt::ptree analytics_configs;
			auto envelope_tree = utility::soap::getEnvelopeTree(ns_);
			envelope_tree.add_child("s:Body.trt:GetVideoAnalyticsConfigurationsResponse.trt:Configurations", analytics_configs);

			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", envelope_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);

			utility::http::fillResponseWithHeaders(*response, os.str());
		}
	};

	struct GetVideoSourceConfigurationHandler : public OnvifRequestBase
	{
		GetVideoSourceConfigurationHandler(const std::map<std::string, std::string>& xs,
			const std::shared_ptr<pt::ptree>& configs,
			const std::shared_ptr<pt::ptree>& profiles_config)
			: OnvifRequestBase(GetVideoSourceConfiguration, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs)
			, profiles_config_(profiles_config)
		{
		}

		void operator()(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request) override
		{
			pt::ptree request_xml;
			pt::xml_parser::read_xml(std::istringstream{ request->content.string() }, request_xml);

			//TODO: add way to search for child with full path like: "Envelope.Body.GetPr..."
			std::string requested_token;
			{
				auto envelope_node = exns::find("Envelope", request_xml);
				auto body_node = exns::find("Body", envelope_node->second);
				auto videosource_node = exns::find("GetVideoSourceConfiguration", body_node->second);
				auto profile_token = exns::find("ConfigurationToken", videosource_node->second);
				requested_token = profile_token->second.get_value<std::string>();
			}

			auto vs_config_list = profiles_config_->get_child("VideoSourceConfigurations");

			auto vs_config = std::find_if(vs_config_list.begin(), vs_config_list.end(),
				[requested_token](pt::ptree::value_type vs_obj)
				{
					return vs_obj.second.get<std::string>("token") == requested_token;
				});

			if (vs_config == vs_config_list.end())
				throw std::runtime_error("The requested configuration indicated with ConfigurationToken does not exist.");

			pt::ptree videosource_configuration_node;
			media::util::fill_soap_videosource_configuration(vs_config->second, videosource_configuration_node);

			auto envelope_tree = utility::soap::getEnvelopeTree(ns_);
			envelope_tree.add_child("s:Body.trt:GetVideoSourceConfigurationResponse.trt:Configuration", videosource_configuration_node);

			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", envelope_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);

			utility::http::fillResponseWithHeaders(*response, os.str());
		}

	private:
		const std::shared_ptr<pt::ptree>& profiles_config_;
	};

	struct GetVideoSourceConfigurationsHandler : public OnvifRequestBase
	{
		GetVideoSourceConfigurationsHandler(const std::map<std::string, std::string>& xs,
			const std::shared_ptr<pt::ptree>& configs,
			const std::shared_ptr<pt::ptree>& profiles_config)
			: OnvifRequestBase(GetVideoSourceConfigurations, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs)
			, profiles_config_(profiles_config)
		{
		}

		void operator()(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request) override
		{
			auto vs_config_list = profiles_config_->get_child("VideoSourceConfigurations");

			pt::ptree vs_configs_node;
			for (const auto& vs_config : vs_config_list)
			{
				pt::ptree vs_config_node;
				media::util::fill_soap_videosource_configuration(vs_config.second, vs_config_node);
				vs_configs_node.add_child("trt:Configurations", vs_config_node);
			}

			auto envelope_tree = utility::soap::getEnvelopeTree(ns_);
			envelope_tree.add_child("s:Body.trt:GetVideoSourceConfigurationsResponse", vs_configs_node);

			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", envelope_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);

			utility::http::fillResponseWithHeaders(*response, os.str());
		}

	private:
		const std::shared_ptr<pt::ptree>& profiles_config_;
	};


	struct GetVideoSourcesHandler : public OnvifRequestBase
	{
		GetVideoSourcesHandler(const std::map<std::string, std::string>& xs,
			const std::shared_ptr<pt::ptree>& configs,
			osrv::ServerConfigs& server_cfg, const std::shared_ptr<pt::ptree>& profiles_config)
			: OnvifRequestBase(GetVideoSources, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs)
			, server_cfg_(server_cfg)
		{
		}

		void operator()(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request) override
		{
			auto envelope_tree = utility::soap::getEnvelopeTree(ns_);

			auto videosources_config = service_configs_->get_child(GetVideoSources);
			pt::ptree response_node;

			//here's Services are enumerates as array, so handle them manualy
			for (auto elements : videosources_config)
			{
				pt::ptree videosource_node;
				videosource_node.put("<xmlattr>.token",
					elements.second.get<std::string>("token"));
				videosource_node.put("tt:Framerate",
					elements.second.get<std::string>("Framerate"));
				videosource_node.put("tt:Resolution.Width",
					elements.second.get<std::string>("Resolution.Width"));
				videosource_node.put("tt:Resolution.Height",
					elements.second.get<std::string>("Resolution.Height"));

				response_node.add_child("trt:VideoSources", videosource_node);

				// simulate multichannel device with the first channel configs
				if (server_cfg_.multichannel_enabled_)
				{
					for (size_t i = 1; i < server_cfg_.channels_count_; ++i)
					{
						std::string token = "VideoSource" + std::to_string(i);
						videosource_node.put("<xmlattr>.token", token);
						response_node.add_child("trt:VideoSources", videosource_node);
					}

					break;
				}

			}

			envelope_tree.add_child("s:Body.trt:GetVideoSourcesResponse", response_node);

			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", envelope_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);

			utility::http::fillResponseWithHeaders(*response, os.str());
		}

	private:
		const osrv::ServerConfigs& server_cfg_;
	};

	struct GetStreamUriHandler : public OnvifRequestBase
	{
		GetStreamUriHandler(const std::map<std::string, std::string>& xs,
			const std::shared_ptr<pt::ptree>& configs,
			osrv::ServerConfigs& server_cfg, const std::shared_ptr<pt::ptree>& profiles_cfg)
			: OnvifRequestBase(GetStreamUri, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs)
			, server_cfg_(server_cfg)
			, profiles_cfg_(profiles_cfg)
		{
		}

		void operator()(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request) override
		{
			pt::ptree request_xml;
			pt::xml_parser::read_xml(std::istringstream{ request->content.string() }, request_xml);

			std::string requested_token = exns::find_hierarchy("Envelope.Body.GetStreamUri.ProfileToken", request_xml);
			//logger_->Debug("Requested token to get URI: " + requested_token);

			if (server_cfg_.multichannel_enabled_)
			{
				requested_token = media::util::MultichannelProfilesNamesConverter(requested_token).CleanedName();
			}

			auto profiles_config_list = profiles_cfg_->get_child("MediaProfiles");

			auto profile_config = std::find_if(profiles_config_list.begin(), profiles_config_list.end(),
				[requested_token](pt::ptree::value_type vs_obj)
				{
					return vs_obj.second.get<std::string>("token") == requested_token;
				});

			if (profile_config == profiles_config_list.end())
				throw std::runtime_error("The media profile does not exist.");

			auto encoder_token = profile_config->second.get<std::string>("VideoEncoderConfiguration");

			auto stream_configs_list = service_configs_->get_child("GetStreamUri");
			auto stream_config_it = std::find_if(stream_configs_list.begin(), stream_configs_list.end(),
				[encoder_token](const pt::ptree::value_type& el)
				{return el.second.get<std::string>("VideoEncoderToken") == encoder_token; });

			if (stream_config_it == stream_configs_list.end())
				throw std::runtime_error("Could not find a stream for the requested Media Profile.");

			pt::ptree response_node;

			auto rtsp_url = media::util::generate_rtsp_url(server_cfg_, stream_config_it->second.get<std::string>("Uri"));
			response_node.put("trt:MediaUri.tt:Uri",
				rtsp_url);
			response_node.put("trt:MediaUri.tt:InvalidAfterConnect",
				stream_config_it->second.get<std::string>("InvalidAfterConnect"));
			response_node.put("trt:MediaUri.tt:InvalidAfterReboot",
				stream_config_it->second.get<std::string>("InvalidAfterReboot"));
			response_node.put("trt:MediaUri.tt:Timeout",
				stream_config_it->second.get<std::string>("Timeout"));

			auto envelope_tree = utility::soap::getEnvelopeTree(ns_);
			envelope_tree.put_child("s:Body.trt:GetStreamUriResponse", response_node);

			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", envelope_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);

			utility::http::fillResponseWithHeaders(*response, os.str());
		}

	private:
		const osrv::ServerConfigs& server_cfg_;
		const std::shared_ptr<pt::ptree>& profiles_cfg_;
	};

	MediaService::MediaService(const std::string& service_uri, const std::string& service_name,
		std::shared_ptr<IOnvifServer> srv)
		: IOnvifService(service_uri, service_name, srv)
	{
		requestHandlers_.push_back(std::make_shared<GetAudioDecoderConfigurationsHandler>(xml_namespaces_, configs_ptree_));
		requestHandlers_.push_back(std::make_shared<GetAudioEncoderConfigurationOptionsHandler>(xml_namespaces_, configs_ptree_, srv->ProfilesConfig()));
		requestHandlers_.push_back(std::make_shared<GetAudioEncoderConfigurationHandler>(xml_namespaces_, configs_ptree_, srv->ProfilesConfig()));
		requestHandlers_.push_back(std::make_shared<GetAudioOutputsHandler>(xml_namespaces_, configs_ptree_));
		requestHandlers_.push_back(std::make_shared<GetAudioSourceConfigurationsHandler>(xml_namespaces_, configs_ptree_));
		requestHandlers_.push_back(std::make_shared<GetAudioSourcesHandler>(xml_namespaces_, configs_ptree_, srv->ProfilesConfig()));
		requestHandlers_.push_back(std::make_shared<GetCompatibleAudioSourceConfigurationsHandler>(xml_namespaces_, configs_ptree_, srv->ProfilesConfig()));
		requestHandlers_.push_back(std::make_shared<GetProfileHandler>(xml_namespaces_, configs_ptree_, *srv->ServerConfigs(), srv->ProfilesConfig()));
		requestHandlers_.push_back(std::make_shared<GetProfilesHandler>(xml_namespaces_, configs_ptree_, *srv->ServerConfigs(), srv->ProfilesConfig()));
		requestHandlers_.push_back(std::make_shared<GetVideoAnalyticsConfigurationsHandler>(xml_namespaces_, configs_ptree_));
		requestHandlers_.push_back(std::make_shared<GetVideoSourceConfigurationHandler>(xml_namespaces_, configs_ptree_, srv->ProfilesConfig()));
		requestHandlers_.push_back(std::make_shared<GetVideoSourceConfigurationsHandler>(xml_namespaces_, configs_ptree_, srv->ProfilesConfig()));
		requestHandlers_.push_back(std::make_shared<GetVideoSourcesHandler>(xml_namespaces_, configs_ptree_, *srv->ServerConfigs(), srv->ProfilesConfig()));
		requestHandlers_.push_back(std::make_shared<GetStreamUriHandler>(xml_namespaces_, configs_ptree_, *srv->ServerConfigs(), srv->ProfilesConfig()));
	}
} // osrv


std::string osrv::media::util::generate_rtsp_url(const ServerConfigs& server_configs, const std::string& profile_stream_url)
{
	std::stringstream rtsp_url;
	rtsp_url << "rtsp://" << server_configs.ipv4_address_ << ":"
		<< (server_configs.enabled_rtsp_port_forwarding ? std::to_string(server_configs.forwarded_rtsp_port)
			: server_configs.rtsp_port_)
		<< "/" << profile_stream_url;

	return rtsp_url.str();
}

void fill_soap_media_profile(const pt::ptree& json_config, pt::ptree& profile_node,
	const std::string& root_node_value, const pt::ptree& profiles_cfg)
{
	profile_node.put("<xmlattr>.token", json_config.get<std::string>("token"));
	profile_node.put("<xmlattr>.fixed", json_config.get<std::string>("fixed"));
	profile_node.put("tt:Name", json_config.get<std::string>("Name"));

	//Videosource
	{
		const std::string vs_token = json_config.get<std::string>("VideoSourceConfiguration");
		auto vs_configs_list = profiles_cfg.get_child("VideoSourceConfigurations");
		auto vs_config = std::find_if(vs_configs_list.begin(), vs_configs_list.end(),
			[vs_token](pt::ptree::value_type vs_obj)
			{
				return vs_obj.second.get<std::string>("token") == vs_token;
			});

		if (vs_config == vs_configs_list.end())
			throw std::runtime_error("Can't find VideoSourceConfiguration with token '" + vs_token + "'");

		pt::ptree videosource_configuration;
		osrv::media::util::fill_soap_videosource_configuration(vs_config->second, videosource_configuration);
		profile_node.put_child("tt:VideoSourceConfiguration", videosource_configuration);
	}

	//VideoEncoder
	{
		auto ve_configs_list = profiles_cfg.get_child("VideoEncoderConfigurations");
		std::string ve_token = json_config.get<std::string>("VideoEncoderConfiguration");
		auto ve_config = std::find_if(ve_configs_list.begin(), ve_configs_list.end(),
			[ve_token](pt::ptree::value_type ve_obj)
			{
				return ve_obj.second.get<std::string>("token") == ve_token;
			});

		if (ve_config == ve_configs_list.end())
			throw std::runtime_error("Can't find VideoEncoderConfiguration with token '" + ve_token + "'");

		profile_node.put("tt:VideoEncoderConfiguration.<xmlattr>.token",
			ve_config->second.get<std::string>("token"));
		profile_node.put("tt:VideoEncoderConfiguration.tt:Name",
			ve_config->second.get<std::string>("Name"));
		profile_node.put("tt:VideoEncoderConfiguration.tt:UseCount",
			ve_config->second.get<std::string>("UseCount"));
		profile_node.put("tt:VideoEncoderConfiguration.tt:Encoding",
			ve_config->second.get<std::string>("Encoding"));
		profile_node.put("tt:VideoEncoderConfiguration.tt:Resolution.tt:Width",
			ve_config->second.get<std::string>("Resolution.Width"));
		profile_node.put("tt:VideoEncoderConfiguration.tt:Resolution.tt:Height",
			ve_config->second.get<std::string>("Resolution.Height"));
		profile_node.put("tt:VideoEncoderConfiguration.tt:Quality",
			ve_config->second.get<std::string>("Quality"));

		//ratecontrol is optional
		auto ratecontrol_config_it = ve_config->second.find("RateControl");
		if (ratecontrol_config_it != ve_config->second.not_found())
		{
			profile_node.put("tt:VideoEncoderConfiguration.tt:RateControl.<xmlattr>.GuaranteedFrameRate",
				ratecontrol_config_it->second.get<std::string>("GuaranteedFrameRate")); // does this should be here ???
			profile_node.put("tt:VideoEncoderConfiguration.tt:RateControl.tt:FrameRateLimit",
				ratecontrol_config_it->second.get<std::string>("FrameRateLimit"));
			profile_node.put("tt:VideoEncoderConfiguration.tt:RateControl.tt:EncodingInterval",
				ratecontrol_config_it->second.get<std::string>("EncodingInterval"));
			profile_node.put("tt:VideoEncoderConfiguration.tt:RateControl.tt:BitrateLimit",
				ratecontrol_config_it->second.get<std::string>("BitrateLimit"));
		}

		const auto& codec = ve_config->second.get<std::string>("Encoding");
		if ("H264" == codec)
		{
			//codecs info is optional
			auto h264_config_it = ve_config->second.find("H264");
			if (h264_config_it != ve_config->second.not_found())
			{
				profile_node.put("tt:VideoEncoderConfiguration.tt:H264.tt:GovLength",
					h264_config_it->second.get<std::string>("GovLength"));
				profile_node.put("tt:VideoEncoderConfiguration.tt:H264.tt:H264Profile",
					h264_config_it->second.get<std::string>("H264Profile"));
			}
		}
		else if ("MPEG4" == codec)
		{
			//TODO
		}

		//Multicast
		profile_node.put("tt:VideoEncoderConfiguration.tt:Multicast.tt:Address.tt:Type",
			ve_config->second.get<std::string>("Multicast.Address.Type"));
		profile_node.put("tt:VideoEncoderConfiguration.tt:Multicast.tt:Address.tt:IPv4Address",
			ve_config->second.get<std::string>("Multicast.Address.IPv4Address"));
		profile_node.put("tt:VideoEncoderConfiguration.tt:Multicast.tt:Port",
			ve_config->second.get<std::string>("Multicast.Port"));
		profile_node.put("tt:VideoEncoderConfiguration.tt:Multicast.tt:TTL",
			ve_config->second.get<std::string>("Multicast.TTL"));
		profile_node.put("tt:VideoEncoderConfiguration.tt:Multicast.tt:AutoStart",
			ve_config->second.get<std::string>("Multicast.AutoStart"));

		profile_node.put("tt:SessionTimeout",
			ve_config->second.get<std::string>("SessionTimeout"));
	}

	//Audio source
	{
		const std::string as_token = json_config.get<std::string>("AudioSourceConfiguration", "");

		if (!as_token.empty())
		{
			pt::ptree as_node;

			auto as_config = utility::AudioSourceConfigsReader(as_token, profiles_cfg).AudioSource();
			as_node.add("<xmlattr>.token", as_token);
			as_node.add("tt:Name", as_config.get<std::string>("Name"));
			as_node.add("tt:UseCount", as_config.get<int>("UseCount"));
			as_node.add("tt:SourceToken", as_config.get<std::string>("SourceToken"));

			profile_node.put_child("tt:AudioSourceConfiguration", as_node);
		}
	}

	// audio encoder
	{
		auto ae_token = json_config.get<std::string>("AudioEncoderConfiguration", "");
		if (!ae_token.empty())
		{
			pt::ptree ae_node;
			auto ae_config = utility::AudioEncoderReaderByToken(ae_token, profiles_cfg).AudioEncoder();

			osrv::media::util::fillAEConfig(ae_config, ae_node);

			profile_node.put_child("tt:AudioEncoderConfiguration", ae_node);
		}
	}
}

void osrv::media::util::fill_soap_videosource_configuration(const pt::ptree& config_node, pt::ptree& videosource_node)
{
	videosource_node.put("<xmlattr>.token",
		config_node.get<std::string>("token"));
	videosource_node.put("tt:Name",
		config_node.get<std::string>("Name"));
	videosource_node.put("tt:UseCount",
		config_node.get<std::string>("UseCount"));
	videosource_node.put("<xmlattr>.ViewMode",
		config_node.get<std::string>("ViewMode"));
	videosource_node.put("tt:SourceToken",
		config_node.get<std::string>("SourceToken"));
	videosource_node.put("tt:Bounds.<xmlattr>.x",
		config_node.get<std::string>("Bounds.x"));
	videosource_node.put("tt:Bounds.<xmlattr>.y",
		config_node.get<std::string>("Bounds.y"));
	videosource_node.put("tt:Bounds.<xmlattr>.width",
		config_node.get<std::string>("Bounds.width"));
	videosource_node.put("tt:Bounds.<xmlattr>.height",
		config_node.get<std::string>("Bounds.height"));
}

void osrv::media::util::fill_analytics_configuration(pt::ptree& result)
{
	result.add("<xmlattr>.token", "analytics_token0");
	result.add("tt:Name", "Analytics0");
	result.add("tt:UseCount", 2);
	result.add("tt:AnalyticsEngineConfgiruation", "");
	result.add("tt:RuleEngineConfiguration", "");
}

void osrv::media::util::fillAEConfig(const pt::ptree& in, pt::ptree& out)
{
	out.add("<xmlattr>.token", in.get<std::string>("token"));
	out.add("tt:Name", in.get<std::string>("Name"));
	out.add("tt:UseCount", in.get<int>("UseCount"));

	// NOTE: IN MEDIA1 there is only G711 and AAC encoding
	// so we need correct name here for PCMU, PCMA and MP4A-LATM.
	auto encoding = in.get<std::string>("Encoding");
	if (encoding == "PCMU" || encoding == "PCMA") encoding = "G711";
	if (encoding == "MP4A-LATM") encoding = "AAC";
	out.add("tt:Encoding", encoding);

	out.add("tt:Bitrate", in.get<int>("Bitrate"));
	out.add("tt:SampleRate", in.get<int>("SampleRate"));

	// TODO: hardcoded
	pt::ptree multicast_node;
	multicast_node.add("tt:Address.tt:Type", "IPv4");
	multicast_node.add("tt:Address.tt:IPv4Address", "0.0.0.0");
	multicast_node.add("tt:Port", 0);
	multicast_node.add("tt:TTL", 0);
	multicast_node.add("tt:AutoStart", false);
	out.add_child("tt:Multicast", multicast_node);

	out.add("tt:SessionTimeout", "PT10S");
}
