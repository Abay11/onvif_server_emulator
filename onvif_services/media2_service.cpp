#include "media2_service.h"
#include "media_service.h" // to use some util functions

#include "../onvif/OnvifRequest.h"

#include "../Logger.h"
#include "../utility/XmlParser.h"
#include "../utility/HttpHelper.h"
#include "../utility/SoapHelper.h"
#include "../utility/AudioSourceReader.h"
#include "../utility/MediaProfilesManager.h"
#include "../Server.h"

#include "../Simple-Web-Server/server_http.hpp"

#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

namespace pt = boost::property_tree;

//the list of implemented methods
static const std::string AddConfiguration = "AddConfiguration";
static const std::string CreateProfile = "CreateProfile";
static const std::string DeleteProfile = "DeleteProfile";
static const std::string GetAnalyticsConfigurations = "GetAnalyticsConfigurations";
static const std::string GetAudioDecoderConfigurations = "GetAudioDecoderConfigurations";
static const std::string GetAudioEncoderConfigurationOptions = "GetAudioEncoderConfigurationOptions";
static const std::string GetAudioEncoderConfigurations = "GetAudioEncoderConfigurations";
static const std::string GetProfiles = "GetProfiles";
static const std::string GetServiceCapabilities = "GetServiceCapabilities";
static const std::string GetStreamUri = "GetStreamUri";
static const std::string GetVideoEncoderConfigurationOptions = "GetVideoEncoderConfigurationOptions";
static const std::string GetVideoEncoderConfigurations = "GetVideoEncoderConfigurations";
static const std::string GetVideoSourceConfigurationOptions = "GetVideoSourceConfigurationOptions";
static const std::string GetVideoSourceConfigurations = "GetVideoSourceConfigurations";
static const std::string SetVideoEncoderConfiguration = "SetVideoEncoderConfiguration";
static const std::string SetVideoSourceConfiguration = "SetVideoSourceConfiguration";

static const std::string CONFIG_PROP_TOKEN{ "token" };

namespace osrv
{
	namespace media2
	{
		struct AddConfigurationHandler : public OnvifRequestBase
		{
			AddConfigurationHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs, const utility::media::MediaProfilesManager* profiles_mgr)
				: OnvifRequestBase(AddConfiguration, auth::SECURITY_LEVELS::ACTUATE, xs, configs)
				, profiles_mgr_(profiles_mgr)
			{
			}

			void operator()(std::shared_ptr<HttpServer::Response> response,
				std::shared_ptr<HttpServer::Request> request) override
			{
				auto envelope_tree = utility::soap::getEnvelopeTree(ns_);

				std::string profile_token;
				std::string cfg_token;
				std::string cfg_type;

				auto request_str = request->content.string();
				std::istringstream is(request_str);
				pt::ptree xml_tree;
				pt::xml_parser::read_xml(is, xml_tree);
				profile_token = exns::find_hierarchy("Envelope.Body.AddConfiguration.ProfileToken", xml_tree);
				cfg_type = exns::find_hierarchy("Envelope.Body.AddConfiguration.Configuration.Type", xml_tree);
				cfg_token = exns::find_hierarchy("Envelope.Body.AddConfiguration.Configuration.Token", xml_tree);

				auto profileTree = profiles_mgr_->GetProfileByToken(profile_token);
				profiles_mgr_->AddConfiguration(utility::media::ProfileConfigsHelper(profileTree).ProfileToken(),
					cfg_type, cfg_token);

				envelope_tree.add("s:Body.tr2:AddConfigurationResponse", "");
				pt::ptree root_tree;
				root_tree.put_child("s:Envelope", envelope_tree);

				std::ostringstream os;
				pt::write_xml(os, root_tree);

				utility::http::fillResponseWithHeaders(*response, os.str());
			}
			
		private:
			const utility::media::MediaProfilesManager* profiles_mgr_;
		};

		struct CreateProfileHandler : public OnvifRequestBase
		{
			CreateProfileHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs, const utility::media::MediaProfilesManager* profiles_mgr)
				: OnvifRequestBase(CreateProfile, auth::SECURITY_LEVELS::ACTUATE, xs, configs)
				, profiles_mgr_(profiles_mgr)
			{
			}

			void operator()(std::shared_ptr<HttpServer::Response> response,
				std::shared_ptr<HttpServer::Request> request) override
			{
				auto envelope_tree = utility::soap::getEnvelopeTree(ns_);

				std::string profile_name;
				std::string cfg_token;
				std::string cfg_type;

				auto request_str = request->content.string();
				std::istringstream is(request_str);
				pt::ptree xml_tree;
				pt::xml_parser::read_xml(is, xml_tree);
				profile_name = exns::find_hierarchy("Envelope.Body.CreateProfile.Name", xml_tree);
				cfg_type = exns::find_hierarchy("Envelope.Body.CreateProfile.Configuration.Type", xml_tree);
				cfg_token = exns::find_hierarchy("Envelope.Body.CreateProfile.Configuration.Token", xml_tree);

				profiles_mgr_->Create(profile_name);

				auto created_profile_token = profiles_mgr_->Back().get<std::string>(CONFIG_PROP_TOKEN);

				if (!cfg_type.empty() && !cfg_token.empty())
					profiles_mgr_->AddConfiguration(created_profile_token, cfg_type, cfg_token);

				envelope_tree.add("s:Body.tr2:CreateProfileResponse.tr2:Token",
					created_profile_token);

				pt::ptree root_tree;
				root_tree.put_child("s:Envelope", envelope_tree);

				std::ostringstream os;
				pt::write_xml(os, root_tree);

				utility::http::fillResponseWithHeaders(*response, os.str());
			}
			
		private:
			const utility::media::MediaProfilesManager* profiles_mgr_;
		};

		struct DeleteProfileHandler : public OnvifRequestBase
		{
			DeleteProfileHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs, const utility::media::MediaProfilesManager* profiles_mgr)
				: OnvifRequestBase(DeleteProfile, auth::SECURITY_LEVELS::ACTUATE, xs, configs)
				, profiles_mgr_(profiles_mgr)
			{
			}

			void operator()(std::shared_ptr<HttpServer::Response> response,
				std::shared_ptr<HttpServer::Request> request) override
			{
				auto envelope_tree = utility::soap::getEnvelopeTree(ns_);

				auto request_str = request->content.string();
				std::istringstream is(request_str);
				pt::ptree xml_tree;
				pt::xml_parser::read_xml(is, xml_tree);
				const auto profile_token = exns::find_hierarchy("Envelope.Body.DeleteProfile.Token", xml_tree);

				profiles_mgr_->Delete(profile_token);

				envelope_tree.add("s:Body.tr2:DeleteProfileResponse", "");

				pt::ptree root_tree;
				root_tree.put_child("s:Envelope", envelope_tree);

				std::ostringstream os;
				pt::write_xml(os, root_tree);

				utility::http::fillResponseWithHeaders(*response, os.str());
			}
			
		private:
			const utility::media::MediaProfilesManager* profiles_mgr_;
		};

		struct GetAnalyticsConfigurationsHandler : public OnvifRequestBase
		{
			GetAnalyticsConfigurationsHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs)
				: OnvifRequestBase(GetAnalyticsConfigurations, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs)
			{
			}

			void operator()(std::shared_ptr<HttpServer::Response> response,
				std::shared_ptr<HttpServer::Request> request) override
			{
				// TODO: the implementation below is hardcoded. fix

				auto envelope_tree = utility::soap::getEnvelopeTree(ns_);

				pt::ptree configurations_tree;
				configurations_tree.add("<xmlattr>.token", "VideoAnalyticsConfigToken0");
				configurations_tree.add("tt:Name", "VideoAnalyticsConfig0");
				configurations_tree.add("tt:Count", "2");
				configurations_tree.add("tt:AnalyticsEngineConfiguration", "");
				configurations_tree.add("tt:RuleEngineConfiguration", "");
				envelope_tree.add_child("s:Body.tr2:GetAnalyticsConfigurationsResponse.tr2:Configurations",
					configurations_tree);

				pt::ptree root_tree;
				root_tree.put_child("s:Envelope", envelope_tree);

				std::ostringstream os;
				pt::write_xml(os, root_tree);

				utility::http::fillResponseWithHeaders(*response, os.str());
			}
		};

		struct GetAudioEncoderConfigurationsHandler : public OnvifRequestBase
		{
		private:
			const std::shared_ptr<pt::ptree>& profiles_configs_;

		public:

			GetAudioEncoderConfigurationsHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs
				, const std::shared_ptr<pt::ptree>& profiles_configs)
				: OnvifRequestBase(GetAudioEncoderConfigurations, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs)
				, profiles_configs_(profiles_configs)
			{
			}

			void operator()(std::shared_ptr<HttpServer::Response> response,
				std::shared_ptr<HttpServer::Request> request) override
			{
				pt::ptree ae_configs_node;

				pt::ptree request_xml_tree;
				pt::xml_parser::read_xml(request->content, request_xml_tree);
				
				auto fillAEConfig = [](const pt::ptree& in, pt::ptree& out) {
					out.add("<xmlattr>.token", in.get<std::string>(CONFIG_PROP_TOKEN));
					out.add("tt:Name", in.get<std::string>("Name"));
					out.add("tt:UseCount", in.get<int>("UseCount"));
					out.add("tt:Encoding", in.get<std::string>("Encoding"));
					out.add("tt:Bitrate", in.get<int>("Bitrate"));
					out.add("tt:SampleRate", in.get<int>("SampleRate"));
				};

				if (auto requestedToken = exns::find_hierarchy("Envelope.Body.GetAudioEncoderConfigurations.ConfigurationToken", request_xml_tree);
					!requestedToken.empty()) // response with a specific AE config
				{
					auto aeCfg = utility::AudioEncoderReaderByToken(requestedToken, *profiles_configs_).AudioEncoder();
					pt::ptree ae_node;
					fillAEConfig(aeCfg, ae_node);
					ae_configs_node.add_child("tr2:Configurations", ae_node);
				}
				else if (auto requestedProfile = exns::find_hierarchy("Envelope.Body.GetAudioEncoderConfigurations.ProfileToken", request_xml_tree);
					!requestedProfile.empty()) // response with a specific profile's AE config
				{
					auto aeCfg = utility::AudioEncoderReaderByProfileToken(requestedProfile, *profiles_configs_).AudioEncoder();
					pt::ptree ae_node;
					fillAEConfig(aeCfg, ae_node);
					ae_configs_node.add_child("tr2:Configurations", ae_node);
				}
				else // response with all existing configs
				{
					const auto ae_config_list = profiles_configs_->get_child(CONFIGURATION_ENUMERATION[CONFIGURATION_TYPE::AUDIOENCODER]);
					for (const auto& ae_config : ae_config_list)
					{
						pt::ptree ae_node;
						fillAEConfig(ae_config.second, ae_node);
						ae_configs_node.add_child("tr2:Configurations", ae_node);
					}
				}
				
				auto env_tree = utility::soap::getEnvelopeTree(ns_);
				env_tree.put_child("s:Body.tr2:GetAudioEncoderConfigurationsResponse", ae_configs_node);
				pt::ptree root_tree;
				root_tree.put_child("s:Envelope", env_tree);

				std::ostringstream os;
				pt::write_xml(os, root_tree);

				utility::http::fillResponseWithHeaders(*response, os.str());
			}
		};

		struct GetAudioEncoderConfigurationOptionsHandler : public OnvifRequestBase
		{
		private:
			const std::shared_ptr<pt::ptree>& profiles_configs_;

		public:

			GetAudioEncoderConfigurationOptionsHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs
				, const std::shared_ptr<pt::ptree>& profiles_configs)
				: OnvifRequestBase(GetAudioEncoderConfigurationOptions, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs)
				, profiles_configs_(profiles_configs)
			{
			}

			void operator()(std::shared_ptr<HttpServer::Response> response,
				std::shared_ptr<HttpServer::Request> request) override
			{
				// TODO: add process options for specific profile or configuration

				auto fillAEOptions= [](const pt::ptree& in, pt::ptree& out) {
					out.add("tt:Encoding", in.get<std::string>("Encoding"));

					for (auto i : in.get_child("BitrateList"))
						out.add("tt:BitrateList.tt:Items", i.second.get_value<int>());

					for (auto i : in.get_child("SampleRateList"))
						out.add("tt:SampleRateList.tt:Items", i.second.get_value<int>());
				};

				pt::ptree ae_opts_node;

				pt::ptree request_xml_tree;
				pt::xml_parser::read_xml(request->content, request_xml_tree);

				{
					const auto ae_config_options_list = profiles_configs_->get_child("AudioEncoderConfigurationOptions");
					for (const auto& options : ae_config_options_list)
					{
						pt::ptree options_node;
						fillAEOptions(options.second, options_node);
						ae_opts_node.add_child("tr2:Options", options_node);
					}
				}

				auto env_tree = utility::soap::getEnvelopeTree(ns_);
				env_tree.put_child("s:Body.tr2:GetAudioEncoderConfigurationOptionsResponse", ae_opts_node);
				pt::ptree root_tree;
				root_tree.put_child("s:Envelope", env_tree);

				std::ostringstream os;
				pt::write_xml(os, root_tree);

				utility::http::fillResponseWithHeaders(*response, os.str());
			}
		};

		struct GetProfilesHandler : public OnvifRequestBase
		{
		private:
			const osrv::ServerConfigs& server_cfg_;
			utility::media::MediaProfilesManager* profiles_mgr_;

		public:

			GetProfilesHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs,
				utility::media::MediaProfilesManager* profiles_mgr, const osrv::ServerConfigs& server_cfg)
				: OnvifRequestBase(GetProfiles, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs)
				, profiles_mgr_(profiles_mgr)
				, server_cfg_(server_cfg)
			{
			}

			void operator()(std::shared_ptr<HttpServer::Response> response,
				std::shared_ptr<HttpServer::Request> request) override
			{
				auto envelope_tree = utility::soap::getEnvelopeTree(ns_);

				const auto& profiles_configs_list = profiles_mgr_->ReaderWriter()->ConfigsTree().get_child("MediaProfiles");

				// extract requested profile token (if there it is) 
				std::string profile_token;
				{
					auto request_str = request->content.string();
					std::istringstream is(request_str);
					pt::ptree xml_tree;
					pt::xml_parser::read_xml(is, xml_tree);
					profile_token = exns::find_hierarchy("Envelope.Body.GetProfiles.Token", xml_tree);
				}

				pt::ptree response_node;
				if (profile_token.empty())
				{
					if (server_cfg_.multichannel_enabled_)
					{
						std::string vsToken = profiles_configs_list.front().second.get<std::string>("VideoSourceConfiguration");
						for (size_t i = 0; i < server_cfg_.channels_count_; ++i)
						{
							auto it = profiles_configs_list.begin();
							while (std::find_if(it, profiles_configs_list.end(),
								[vsToken](const pt::ptree::value_type& tree)
								{
									return tree.second.get<std::string>("VideoSourceConfiguration") == vsToken;
								}) != profiles_configs_list.end())
							{
								pt::ptree profile_node;
								media2::util::profile_to_soap(it->second, profiles_mgr_->ReaderWriter()->ConfigsTree(), profile_node);

								std::string profileToken = std::to_string(i) + "_"
									+ it->second.get<std::string>(CONFIG_PROP_TOKEN);
								profile_node.put("<xmlattr>.token", profileToken);

								std::string vsToken = "VideoSource" + std::to_string(i);
								profile_node.put("tr2:Configurations.tr2:VideoSource.tt:SourceToken", vsToken);

								response_node.add_child("tr2:Profiles", profile_node);
								++it;
							}
						}
					}
					else
					{
						// response all media profiles' configs
						for (const auto& elements : profiles_configs_list)
						{
							pt::ptree profile_node;
							media2::util::profile_to_soap(elements.second, profiles_mgr_->ReaderWriter()->ConfigsTree(), profile_node);
							response_node.add_child("tr2:Profiles", profile_node);
						}
					}
				}
				else
				{
					// response only one profile's configs

					std::string cleanedName = media::util::MultichannelProfilesNamesConverter(profile_token).CleanedName();

					const auto& profile_config = profiles_mgr_->GetProfileByToken(cleanedName);

					pt::ptree profile_node;
					media2::util::profile_to_soap(profile_config, profiles_mgr_->ReaderWriter()->ConfigsTree(), profile_node);

					if (server_cfg_.multichannel_enabled_)
						profile_node.put("<xmlattr>.token", profile_token);

					response_node.add_child("tr2:Profiles", profile_node);
				}

				envelope_tree.put_child("s:Body.tr2:GetProfilesResponse", response_node);

				pt::ptree root_tree;
				root_tree.put_child("s:Envelope", envelope_tree);

				std::ostringstream os;
				pt::write_xml(os, root_tree);

				utility::http::fillResponseWithHeaders(*response, os.str());
			}
		};

		struct GetVideoEncoderConfigurationsHandler : public OnvifRequestBase
		{
		private:
			const std::shared_ptr<pt::ptree>& profiles_configs_;

		public:

			GetVideoEncoderConfigurationsHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs
				, const std::shared_ptr<pt::ptree>& profiles_configs)
				: OnvifRequestBase(GetVideoEncoderConfigurations, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs)
				, profiles_configs_(profiles_configs)
			{
			}

			void operator()(std::shared_ptr<HttpServer::Response> response,
				std::shared_ptr<HttpServer::Request> request) override
			{
				const auto ve_config_list = profiles_configs_->get_child(CONFIGURATION_ENUMERATION[CONFIGURATION_TYPE::VIDEOENCODER]);
				pt::ptree ve_configs_node;
				for (const auto& ve_config : ve_config_list)
				{
					pt::ptree videoencoder_configuration;
					osrv::media2::util::fill_video_encoder(ve_config.second, videoencoder_configuration);
					ve_configs_node.add_child("tr2:Configurations", videoencoder_configuration);
				}

				auto env_tree = utility::soap::getEnvelopeTree(ns_);
				env_tree.put_child("s:Body.tr2:GetVideoEncoderConfigurationsResponse", ve_configs_node);
				pt::ptree root_tree;
				root_tree.put_child("s:Envelope", env_tree);

				std::ostringstream os;
				pt::write_xml(os, root_tree);

				utility::http::fillResponseWithHeaders(*response, os.str());
			}
		};

		struct GetVideoEncoderConfigurationOptionsHandler : public OnvifRequestBase
		{
		private:
			const std::shared_ptr<pt::ptree>& profiles_configs_;

		public:

			GetVideoEncoderConfigurationOptionsHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs
				, const std::shared_ptr<pt::ptree>& profiles_configs)
				: OnvifRequestBase(GetVideoEncoderConfigurationOptions, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs)
				, profiles_configs_(profiles_configs)
			{
			}

			void operator()(std::shared_ptr<HttpServer::Response> response,
				std::shared_ptr<HttpServer::Request> request) override
			{
			// a request may contain specific config token or profile token,
			// but for now, I no see reason somehow filter and process that additional conditions
			// so just return "as is" from config files all encoding options

			//// extract requested encoder configuration token 
			std::string configuration_token;
			std::string profile_token;
			{
				auto request_str = request->content.string();
				std::istringstream is(request_str);	
				pt::ptree xml_tree;
				pt::xml_parser::read_xml(is, xml_tree);
				configuration_token = exns::find_hierarchy("Envelope.Body.GetVideoEncoderConfigurationOptions.ConfigurationToken", xml_tree);
				profile_token = exns::find_hierarchy("Envelope.Body.GetVideoEncoderConfigurationOptions.ProfileToken", xml_tree);
			}

			if (!profile_token.empty())
			{
				const auto profiles_configs_list = profiles_configs_->get_child("MediaProfiles");

				auto req_profile_config = std::find_if(profiles_configs_list.begin(), profiles_configs_list.end(),
					[profile_token](const pt::ptree::value_type& it)
					{
						return it.second.get<std::string>(CONFIG_PROP_TOKEN) == profile_token;
					});

				if (req_profile_config == profiles_configs_list.end())
				{
					throw std::runtime_error("Client error: Not found requeried profile token!");
				}
			}

			const auto enc_config_options_list = profiles_configs_->get_child("VideoEncoderConfigurationOptions2");

			pt::ptree response_node;
			for (const auto& ec : enc_config_options_list)
			{
				pt::ptree option_node;

				{ //GOV length
					option_node.add("<xmlattr>.GovLengthRange", ec.second.get<std::string>("GovLengthRange"));
				}

				{ //FrameRatesSupported 

					std::vector<float> framerates;
					auto fps_list = ec.second.get_child("FrameRatesSupported");
					for (auto n : fps_list)
					{
						framerates.push_back(n.second.get_value<float>());
					}
					option_node.add("<xmlattr>.FrameRatesSupported", media2::util::to_value_list(framerates));
				}

				{ //ProfilesSupported 

					std::vector<std::string> profiles;
					auto fps_list = ec.second.get_child("ProfilesSupported");
					for (auto n : fps_list)
					{
						profiles.push_back(n.second.get_value<std::string>());
					}
					option_node.add("<xmlattr>.ProfilesSupported", media2::util::to_value_list(profiles));
				}
				
				option_node.add("<xmlattr>.ConstantBitRateSupported", ec.second.get<bool>("ConstantBitRateSupported"));

				option_node.add("<xmlattr>.GuaranteedFrameRateSupported", ec.second.get<bool>("GuaranteedFrameRateSupported"));


				option_node.add("tt:Encoding ", ec.second.get<std::string>("Encoding"));
				
				option_node.add("tt:QualityRange.tt:Min", ec.second.get<float>("QualityRange.Min"));
				option_node.add("tt:QualityRange.tt:Max", ec.second.get<float>("QualityRange.Max"));

				option_node.add("tt:ResolutionsAvailable.tt:Width", ec.second.get<float>("ResolutionsAvailable.Width"));
				option_node.add("tt:ResolutionsAvailable.tt:Height", ec.second.get<float>("ResolutionsAvailable.Height"));

				option_node.add("tt:BitrateRange.tt:Min", ec.second.get<int>("BitrateRange.Min"));
				option_node.add("tt:BitrateRange.tt:Max", ec.second.get<int>("BitrateRange.Max"));

				response_node.add_child("tr2:Options", option_node);
			}

			auto env_tree = utility::soap::getEnvelopeTree(ns_);
			env_tree.put_child("s:Body.tr2:GetVideoEncoderConfigurationOptionsResponse", response_node);
			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", env_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);

			utility::http::fillResponseWithHeaders(*response, os.str());
			}
		};
		
		struct GetVideoSourceConfigurationsHandler : public OnvifRequestBase
		{
		private:
			const std::shared_ptr<pt::ptree>& profiles_configs_;
			const osrv::ServerConfigs& server_cfg_;

		public:

			GetVideoSourceConfigurationsHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs
				, const std::shared_ptr<pt::ptree>& profiles_configs, const osrv::ServerConfigs& server_cfg)
				: OnvifRequestBase(GetVideoSourceConfigurations, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs)
				, profiles_configs_(profiles_configs)
				, server_cfg_(server_cfg)
			{
			}

			void operator()(std::shared_ptr<HttpServer::Response> response,
				std::shared_ptr<HttpServer::Request> request) override
			{
				auto vs_config_list = profiles_configs_->get_child(CONFIGURATION_ENUMERATION[CONFIGURATION_TYPE::VIDEOSOURCE]);
				pt::ptree vs_configs_node;
				for (const auto& vs_config : vs_config_list)
				{
					pt::ptree videosource_configuration;
					osrv::media::util::fill_soap_videosource_configuration(vs_config.second, videosource_configuration);
					vs_configs_node.put_child("tr2:Configurations", videosource_configuration);

					// multichannel simulaiton with the first channel configs
					if (server_cfg_.multichannel_enabled_)
					{
						for (size_t i = 0; i < server_cfg_.channels_count_ - 1; ++i)
						{
							vs_configs_node.put_child("tr2:Configurations", videosource_configuration);
						}

						break;
					}
				}

				auto env_tree = utility::soap::getEnvelopeTree(ns_);
				env_tree.put_child("s:Body.tr2:GetVideoSourceConfigurationsResponse", vs_configs_node);
				pt::ptree root_tree;
				root_tree.put_child("s:Envelope", env_tree);

				std::ostringstream os;
				pt::write_xml(os, root_tree);

				utility::http::fillResponseWithHeaders(*response, os.str());
			}
		};

		struct GetVideoSourceConfigurationOptionsHandler : public OnvifRequestBase
		{
		private:
			const std::shared_ptr<pt::ptree>& profiles_configs_;

		public:

			GetVideoSourceConfigurationOptionsHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs
				, const std::shared_ptr<pt::ptree>& profiles_configs)
				: OnvifRequestBase(GetVideoSourceConfigurationOptions, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs)
				, profiles_configs_(profiles_configs)
			{
			}

			void operator()(std::shared_ptr<HttpServer::Response> response,
				std::shared_ptr<HttpServer::Request> request) override
			{
				// TODO:
				// Here we should parse request and generate a response depends on required profile token and videosource
				// configuration token, but for now it's ignored

				auto vs_config_list = profiles_configs_->get_child(CONFIGURATION_ENUMERATION[CONFIGURATION_TYPE::VIDEOSOURCE]);
				pt::ptree options_node;
				for (const auto& vs_config : vs_config_list)
				{
					pt::ptree option;
					option.add("<xmlattr>.MaximumNumberOfProfiles", vs_config.second.get<int>("Options.MaximumNumberOfProfiles"));

					option.add("tt:BoundsRange.tt:XRange.tt:Min",
						vs_config.second.get<int>("Options.BoundsRange.XRange.Min"));
					option.add("tt:BoundsRange.tt:XRange.tt:Max",
						vs_config.second.get<int>("Options.BoundsRange.XRange.Max"));

					option.add("tt:BoundsRange.tt:YRange.tt:Min",
						vs_config.second.get<int>("Options.BoundsRange.YRange.Min"));
					option.add("tt:BoundsRange.tt:YRange.tt:Max",
						vs_config.second.get<int>("Options.BoundsRange.YRange.Max"));

					option.add("tt:BoundsRange.tt:WidthRange.tt:Min",
						vs_config.second.get<int>("Options.BoundsRange.WidthRange.Min"));
					option.add("tt:BoundsRange.tt:WidthRange.tt:Max",
						vs_config.second.get<int>("Options.BoundsRange.WidthRange.Max"));

					option.add("tt:BoundsRange.tt:HeightRange.tt:Min",
						vs_config.second.get<int>("Options.BoundsRange.HeightRange.Min"));
					option.add("tt:BoundsRange.tt:HeightRange.tt:Max",
						vs_config.second.get<int>("Options.BoundsRange.HeightRange.Max"));

					pt::ptree rotate;
					rotate.add("tt:Mode", "OFF");
					rotate.add("tt:Mode", "ON");
					rotate.add("tt:Mode", "AUTO");
					rotate.add("tt:DegreeList.tt:Items", 0);

					pt::ptree extentions_node;
					extentions_node.add_child("tt:Rotate", rotate);

					// one more inner extention for SceneOrientationMode
					extentions_node.add("tt:Extention.tt:SceneOrientationMode", "AUTO");

					option.add_child("tt:Extention", extentions_node);

					options_node.put_child("tr2:Options", option);
				}

				auto env_tree = utility::soap::getEnvelopeTree(ns_);
				env_tree.put_child("s:Body.tr2:GetVideoSourceConfigurationOptionsResponse", options_node);
				pt::ptree root_tree;
				root_tree.put_child("s:Envelope", env_tree);

				std::ostringstream os;
				pt::write_xml(os, root_tree);

				utility::http::fillResponseWithHeaders(*response, os.str());
			}
		};

		struct GetServiceCapabilitiesHandler : public OnvifRequestBase
		{
		private:

		public:

			GetServiceCapabilitiesHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs)
				: OnvifRequestBase(GetServiceCapabilities, auth::SECURITY_LEVELS::PRE_AUTH, xs, configs)
			{
			}

			void operator()(std::shared_ptr<HttpServer::Response> response,
				std::shared_ptr<HttpServer::Request> request) override
			{
				auto capabilities_config = service_configs_->get_child("GetServiceCapabilities2");
				pt::ptree capabilities_node;
				capabilities_node.add("<xmlattr>.SnapshotUri", capabilities_config.get<bool>("SnapshotUri"));
				capabilities_node.add("<xmlattr>.Rotation", capabilities_config.get<bool>("Rotation"));
				capabilities_node.add("<xmlattr>.VideoSourceMode", capabilities_config.get<bool>("VideoSourceMode"));
				capabilities_node.add("<xmlattr>.OSD", capabilities_config.get<bool>("OSD"));
				capabilities_node.add("<xmlattr>.TemporaryOSDText", capabilities_config.get<bool>("TemporaryOSDText"));
				capabilities_node.add("<xmlattr>.Mask", capabilities_config.get<bool>("Mask"));
				capabilities_node.add("<xmlattr>.SourceMask", capabilities_config.get<bool>("SourceMask"));
				
				capabilities_node.add("tr2:ProfileCapabilities.<xmlattr>.MaximumNumberOfProfiles",
					capabilities_config.get<int>("ProfileCapabilities.MaximumNumberOfProfiles"));
				capabilities_node.add("tr2:ProfileCapabilities.<xmlattr>.ConfigurationsSupported",
					capabilities_config.get<std::string>("ProfileCapabilities.ConfigurationsSupported"));

				capabilities_node.add("tr2:StreamingCapabilities.<xmlattr>.RTSPStreaming", capabilities_config.get<bool>("StreamingCapabilities.RTSPStreaming"));
				capabilities_node.add("tr2:StreamingCapabilities.<xmlattr>.RTPMulticast", capabilities_config.get<bool>("StreamingCapabilities.RTPMulticast"));
				capabilities_node.add("tr2:StreamingCapabilities.<xmlattr>.RTP_RTSP_TCP", capabilities_config.get<bool>("StreamingCapabilities.RTP_RTSP_TCP"));
				capabilities_node.add("tr2:StreamingCapabilities.<xmlattr>.NonAggregateControl", capabilities_config.get<bool>("StreamingCapabilities.NonAggregateControl"));
				capabilities_node.add("tr2:StreamingCapabilities.<xmlattr>.AutoStartMulticast", capabilities_config.get<bool>("StreamingCapabilities.AutoStartMulticast"));

				auto env_tree = utility::soap::getEnvelopeTree(ns_);
				env_tree.put_child("s:Body.tr2:GetServiceCapabilitiesResponse.tr2:Capabilities", capabilities_node);
				pt::ptree root_tree;
				root_tree.put_child("s:Envelope", env_tree);

				std::ostringstream os;
				pt::write_xml(os, root_tree);

				utility::http::fillResponseWithHeaders(*response, os.str());
			}
		};

		struct GetStreamUriHandler : public OnvifRequestBase
		{
		private:
			const std::shared_ptr<pt::ptree>& profiles_configs_;
			const osrv::ServerConfigs& server_cfg_;

		public:

			GetStreamUriHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs
				, const std::shared_ptr<pt::ptree>& profiles_configs, const osrv::ServerConfigs& server_cfg)
				: OnvifRequestBase(GetStreamUri, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs)
				, profiles_configs_(profiles_configs)
				, server_cfg_(server_cfg)
			{
			}

			void operator()(std::shared_ptr<HttpServer::Response> response,
				std::shared_ptr<HttpServer::Request> request) override
			{
				pt::ptree request_xml;
				pt::xml_parser::read_xml(std::istringstream{ request->content.string() }, request_xml);

				std::string requested_token = exns::find_hierarchy("Envelope.Body.GetStreamUri.ProfileToken", request_xml);

				if (server_cfg_.multichannel_enabled_)
				{
					requested_token = media::util::MultichannelProfilesNamesConverter(requested_token).CleanedName();
				}

				//logger_->Debug("Requested token to get URI=" + requested_token);

				auto profiles_config_list = profiles_configs_->get_child("MediaProfiles");

				auto profile_config = std::find_if(profiles_config_list.begin(), profiles_config_list.end(),
					[requested_token](const pt::ptree::value_type& vs_obj)
					{
						return vs_obj.second.get<std::string>(CONFIG_PROP_TOKEN) == requested_token;
					});

				if (profile_config == profiles_config_list.end())
					throw std::runtime_error("Can't find a proper URI: the media profile does not exist. token=" + requested_token);

				auto encoder_token = profile_config->second.get<std::string>("VideoEncoderConfiguration");

				auto stream_configs_list = service_configs_->get_child("GetStreamUri");
				auto stream_config_it = std::find_if(stream_configs_list.begin(), stream_configs_list.end(),
					[encoder_token](const pt::ptree::value_type& el)
					{return el.second.get<std::string>("VideoEncoderToken") == encoder_token; });

				if (stream_config_it == stream_configs_list.end())
					throw std::runtime_error("Could not find a stream for the requested Media Profile token=" + requested_token);

				pt::ptree response_node;
				auto rtsp_url = media2::util::generate_rtsp_url(server_cfg_, stream_config_it->second.get<std::string>("Uri"));
				response_node.put("tr2:Uri", rtsp_url);

				auto envelope_tree = utility::soap::getEnvelopeTree(ns_);
				envelope_tree.put_child("s:Body.tr2:GetStreamUriResponse", response_node);

				pt::ptree root_tree;
				root_tree.put_child("s:Envelope", envelope_tree);

				std::ostringstream os;
				pt::write_xml(os, root_tree);

				utility::http::fillResponseWithHeaders(*response, os.str());
			}
		};

		struct SetVideoEncoderConfigurationHandler : public OnvifRequestBase
		{
		private:

		public:

			SetVideoEncoderConfigurationHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs)
				: OnvifRequestBase(SetVideoEncoderConfiguration, auth::SECURITY_LEVELS::ACTUATE, xs, configs)
			{
			}

			void operator()(std::shared_ptr<HttpServer::Response> response,
				std::shared_ptr<HttpServer::Request> request) override
			{
				pt::ptree request_xml;
				pt::xml_parser::read_xml(std::istringstream{ request->content.string() }, request_xml);

				// TODO: add impmlementation

				auto envelope_tree = utility::soap::getEnvelopeTree(ns_);
				envelope_tree.put("s:Body.tr2:SetVideoEncoderConfigurationResponse", "");

				pt::ptree root_tree;
				root_tree.put_child("s:Envelope", envelope_tree);

				std::ostringstream os;
				pt::write_xml(os, root_tree);

				utility::http::fillResponseWithHeaders(*response, os.str());
			}
		};

		struct SetVideoSourceConfigurationHandler : public OnvifRequestBase
		{
		private:

		public:

			SetVideoSourceConfigurationHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs)
				: OnvifRequestBase(SetVideoSourceConfiguration, auth::SECURITY_LEVELS::ACTUATE, xs, configs)
			{
			}

			void operator()(std::shared_ptr<HttpServer::Response> response,
				std::shared_ptr<HttpServer::Request> request) override
			{
				pt::ptree request_xml;
				pt::xml_parser::read_xml(std::istringstream{ request->content.string() }, request_xml);

				// TODO: add implementation

				auto envelope_tree = utility::soap::getEnvelopeTree(ns_);
				envelope_tree.put("s:Body.tr2:SetVideoSourceConfigurationResponse", "");

				pt::ptree root_tree;
				root_tree.put_child("s:Envelope", envelope_tree);

				std::ostringstream os;
				pt::write_xml(os, root_tree);

				utility::http::fillResponseWithHeaders(*response, os.str());
			}
		};

		namespace util
		{
			std::string generate_rtsp_url(const ServerConfigs& server_configs, const std::string& profile_stream_url)
			{
				std::stringstream rtsp_url;
				rtsp_url << "rtsp://" << server_configs.ipv4_address_ << ":"
					<< (server_configs.enabled_rtsp_port_forwarding ? std::to_string(server_configs.forwarded_rtsp_port)
						: server_configs.rtsp_port_)
					<< "/" << profile_stream_url;

				return rtsp_url.str();
			}

			using ptree = boost::property_tree::ptree;
			void profile_to_soap(const ptree& profile_config, const ptree& configs_file, ptree& result)
			{
				result.add("<xmlattr>.token", profile_config.get<std::string>(CONFIG_PROP_TOKEN));
				result.add("<xmlattr>.fixed", profile_config.get<std::string>("fixed"));
				result.add("tr2:Name", profile_config.get<std::string>("Name"));

				static const std::string DEFAULT_EMPTY_STRING;

				//Videosource
				const std::string vs_token = profile_config.get<std::string>(CONFIGURATION_ENUMERATION[CONFIGURATION_TYPE::VIDEOSOURCE], DEFAULT_EMPTY_STRING);
				if (!vs_token.empty())
				{
					auto vs_configs_list = configs_file.get_child(CONFIGURATION_ENUMERATION[CONFIGURATION_TYPE::VIDEOSOURCE]);
					auto vs_config = std::find_if(vs_configs_list.begin(), vs_configs_list.end(),
						[vs_token](pt::ptree::value_type vs_obj)
						{
							return vs_obj.second.get<std::string>(CONFIG_PROP_TOKEN) == vs_token;
						});

					if (vs_config == vs_configs_list.end())
						throw std::runtime_error("Can't find VideoSourceConfiguration with token '" + vs_token + "'");

					pt::ptree videosource_configuration;
					osrv::media::util::fill_soap_videosource_configuration(vs_config->second, videosource_configuration);
					result.put_child("tr2:Configurations.tr2:VideoSource", videosource_configuration);
				}

				// videoencoder
				const std::string ve_token = profile_config.get<std::string>(CONFIGURATION_ENUMERATION[CONFIGURATION_TYPE::VIDEOENCODER], DEFAULT_EMPTY_STRING);
				if (!ve_token.empty())
				{
					//TODO: use the same configuartion structure with Media1  --->get_child("VideoEncoderConfigurations2")
					auto vs_configs_list = configs_file.get_child(CONFIGURATION_ENUMERATION[CONFIGURATION_TYPE::VIDEOENCODER]);
					auto vs_config = std::find_if(vs_configs_list.begin(), vs_configs_list.end(),
						[&ve_token](const pt::ptree::value_type& vs_obj)
						{
							return vs_obj.second.get<std::string>(CONFIG_PROP_TOKEN) == ve_token;
						});

					if (vs_config == vs_configs_list.end())
						throw std::runtime_error("Can't find VideoEncoderConfiguration with token '" + ve_token + "'");
					pt::ptree videoencoder_configuration;
					osrv::media2::util::fill_video_encoder(vs_config->second, videoencoder_configuration);
					result.put_child("tr2:Configurations.tr2:VideoEncoder", videoencoder_configuration);
				}

				// Videoanalytics
				const std::string va_token = profile_config.get<std::string>(CONFIGURATION_ENUMERATION[CONFIGURATION_TYPE::ANALYTICS], DEFAULT_EMPTY_STRING);
				if (!va_token.empty())
				{
					// just fill dummy configs
					pt::ptree analytics_node;
					osrv::media::util::fill_analytics_configuration(analytics_node);
					result.put_child("tr2:Configurations.tr2:Analytics", analytics_node);
				}
	
				// PTZ
				const std::string ptz_token = profile_config.get<std::string>(CONFIGURATION_ENUMERATION[CONFIGURATION_TYPE::PTZ], DEFAULT_EMPTY_STRING);
				if (!ptz_token.empty())
				{ 
					// TODO: make reading from a profile
					pt::ptree ptz_node;
					ptz_node.add("<xmlattr>.token", "PtzConfigToken0");
					ptz_node.add("tt:Name", "PtzConfig0");
					ptz_node.add("tt:UseCount", 3);
					ptz_node.add("tt:NodeToken", "PTZNODE_1");
					ptz_node.add("tt:DefaultContinuousPanTiltVelocitySpace", 
						"http://www.onvif.org/ver10/tptz/PanTiltSpaces/VelocityGenericSpace");
					ptz_node.add("tt:DefaultContinuousZoomVelocitySpace",
						"http://www.onvif.org/ver10/tptz/ZoomSpaces/VelocityGenericSpace");
					result.put_child("tr2:Configurations.tr2:PTZ", ptz_node);
				}

				// Audio source
				auto as_token = profile_config.get<std::string>(CONFIGURATION_ENUMERATION[CONFIGURATION_TYPE::AUDIOSOURCE], DEFAULT_EMPTY_STRING);
				if (!as_token.empty())
				{
					pt::ptree as_node;
					auto as_config = utility::AudioSourceConfigsReader(as_token, configs_file).AudioSource();
					as_node.add("<xmlattr>.token", as_token);
					as_node.add("tt:Name", as_config.get<std::string>("Name"));
					as_node.add("tt:UseCount", as_config.get<int>("UseCount"));
					as_node.add("tt:SourceToken", as_config.get<std::string>("SourceToken"));

					result.put_child("tr2:Configurations.tr2:AudioSource", as_node);
				}

				// Audio encoder
				auto ae_token = profile_config.get<std::string>(CONFIGURATION_ENUMERATION[CONFIGURATION_TYPE::AUDIOENCODER], DEFAULT_EMPTY_STRING);
				if (!ae_token.empty())
				{
					pt::ptree ae_node;
					auto ae_config = utility::AudioEncoderReaderByToken(ae_token, configs_file).AudioEncoder();
					ae_node.add("<xmlattr>.token", ae_token);
					ae_node.add("tt:Name", ae_config.get<std::string>("Name"));
					ae_node.add("tt:UseCount", ae_config.get<int>("UseCount"));
					ae_node.add("tt:Encoding", ae_config.get<std::string>("Encoding"));
					ae_node.add("tt:Bitrate", ae_config.get<int>("Bitrate"));
					ae_node.add("tt:SampleRate", ae_config.get<int>("SampleRate"));

					result.put_child("tr2:Configurations.tr2:AudioEncoder", ae_node);
				}
			}
			
			void fill_video_encoder(const pt::ptree& config_node, pt::ptree& videoencoder_node)
			{
				videoencoder_node.add("<xmlattr>.token", config_node.get<std::string>(CONFIG_PROP_TOKEN));
				videoencoder_node.add("tt:Name", config_node.get<std::string>("Name"));
				videoencoder_node.add("tt:UseCount", config_node.get<int>("UseCount"));
				videoencoder_node.add("<xmlattr>.GovLength", config_node.get<int>("GovLength"));
				videoencoder_node.add("<xmlattr>.Profile", config_node.get<std::string>("Profile"));
				videoencoder_node.add("<xmlattr>.GuaranteedFrameRate", config_node.get<bool>("GuaranteedFrameRate"));
				videoencoder_node.add("tt:Encoding", config_node.get<std::string>("Encoding"));
				videoencoder_node.add("tt:Resolution.tt:Width", config_node.get<int>("Resolution.Width"));
				videoencoder_node.add("tt:Resolution.tt:Height", config_node.get<int>("Resolution.Height"));
				videoencoder_node.add("tt:Quality", config_node.get<float>("Quality"));
				videoencoder_node.add("tt:RateControl.tt:ConstantBitRate", config_node.get<bool>("RateControl.ConstantBitRate"));
				videoencoder_node.add("tt:RateControl.tt:FrameRateLimit", config_node.get<float>("RateControl.FrameRateLimit"));
				videoencoder_node.add("tt:RateControl.tt:BitrateLimit", config_node.get<int>("RateControl.BitrateLimit"));
			}
		} // util
    } // media2

	Media2Service::Media2Service(const std::string& service_uri, const std::string& service_name,
		std::shared_ptr<IOnvifServer> srv)
		: IOnvifService(service_uri, service_name, srv)
	{
		requestHandlers_.push_back(std::make_shared<media2::AddConfigurationHandler>(xml_namespaces_, configs_ptree_, srv->MediaProfilesManager()));
		requestHandlers_.push_back(std::make_shared<media2::CreateProfileHandler>(xml_namespaces_, configs_ptree_, srv->MediaProfilesManager()));
		requestHandlers_.push_back(std::make_shared<media2::DeleteProfileHandler>(xml_namespaces_, configs_ptree_, srv->MediaProfilesManager()));
		requestHandlers_.push_back(std::make_shared<media2::GetAnalyticsConfigurationsHandler>(xml_namespaces_, configs_ptree_));
		requestHandlers_.push_back(std::make_shared<media2::GetAudioEncoderConfigurationOptionsHandler>(xml_namespaces_, configs_ptree_, srv->ProfilesConfig()));
		requestHandlers_.push_back(std::make_shared<media2::GetAudioEncoderConfigurationsHandler>(xml_namespaces_, configs_ptree_, srv->ProfilesConfig()));
		requestHandlers_.push_back(std::make_shared<media2::GetProfilesHandler>(xml_namespaces_, configs_ptree_, srv->MediaProfilesManager(), *srv->ServerConfigs()));
		requestHandlers_.push_back(std::make_shared<media2::GetServiceCapabilitiesHandler>(xml_namespaces_, configs_ptree_));
		requestHandlers_.push_back(std::make_shared<media2::GetStreamUriHandler>(xml_namespaces_, configs_ptree_, srv->ProfilesConfig(), *srv->ServerConfigs()));
		requestHandlers_.push_back(std::make_shared<media2::GetVideoEncoderConfigurationOptionsHandler>(xml_namespaces_, configs_ptree_, srv->ProfilesConfig()));
		requestHandlers_.push_back(std::make_shared<media2::GetVideoEncoderConfigurationsHandler>(xml_namespaces_, configs_ptree_, srv->ProfilesConfig()));
		requestHandlers_.push_back(std::make_shared<media2::GetVideoSourceConfigurationOptionsHandler>(xml_namespaces_, configs_ptree_, srv->ProfilesConfig()));
		requestHandlers_.push_back(std::make_shared<media2::GetVideoSourceConfigurationsHandler>(xml_namespaces_, configs_ptree_, srv->ProfilesConfig(), *srv->ServerConfigs()));
		requestHandlers_.push_back(std::make_shared<media2::SetVideoEncoderConfigurationHandler>(xml_namespaces_, configs_ptree_));
		requestHandlers_.push_back(std::make_shared<media2::SetVideoSourceConfigurationHandler>(xml_namespaces_, configs_ptree_));
	}
}
