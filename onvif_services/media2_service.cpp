#include "media2_service.h"
#include "media_service.h" // to use some util functions

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

static ILogger* logger_ = nullptr;

static const osrv::ServerConfigs* server_configs;
static std::shared_ptr<utility::digest::IDigestSession> digest_session;

static const std::string PROFILES_CONFIGS_PATH = "media_profiles.config";
static const std::string MEDIA_SERVICE_CONFIGS_PATH = "media2.config";

namespace pt = boost::property_tree;
static pt::ptree CONFIGS_TREE;
static pt::ptree PROFILES_CONFIGS_TREE;
static std::map<std::string, std::string> XML_NAMESPACES;

//the list of implemented methods
static const std::string GetAnalyticsConfigurations = "GetAnalyticsConfigurations";
static const std::string GetAudioEncoderConfigurations = "GetAudioEncoderConfigurations";
static const std::string GetAudioEncoderConfigurationOptions = "GetAudioEncoderConfigurationOptions";
static const std::string GetAudioDecoderConfigurations = "GetAudioDecoderConfigurations";
static const std::string GetProfiles = "GetProfiles";
static const std::string GetVideoEncoderConfigurations = "GetVideoEncoderConfigurations";
static const std::string GetVideoEncoderConfigurationOptions = "GetVideoEncoderConfigurationOptions";
static const std::string GetVideoSourceConfigurations = "GetVideoSourceConfigurations";
static const std::string GetVideoSourceConfigurationOptions = "GetVideoSourceConfigurationOptions";
static const std::string GetServiceCapabilities = "GetServiceCapabilities";
static const std::string GetStreamUri = "GetStreamUri";
static const std::string SetVideoEncoderConfiguration = "SetVideoEncoderConfiguration";
static const std::string SetVideoSourceConfiguration = "SetVideoSourceConfiguration";

namespace osrv
{
    namespace media2
    {
		//using handler_t = void(std::shared_ptr<HttpServer::Response> response,
		//	std::shared_ptr<HttpServer::Request> request);
		//static std::map<std::string, handler_t*> handlers;

		static std::vector<utility::http::HandlerSP> handlers;
		
		
		const boost::property_tree::ptree& config_instance()
		{
			return CONFIGS_TREE;
		}

		void do_handler_request(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request);

		struct GetAnalyticsConfigurationsHandler : public utility::http::RequestHandlerBase
		{
			GetAnalyticsConfigurationsHandler() : utility::http::RequestHandlerBase(GetAnalyticsConfigurations, osrv::auth::SECURITY_LEVELS::READ_MEDIA)
			{
			}

			OVERLOAD_REQUEST_HANDLER
			{
				// TODO: the implementation below is hardcoded. fix

				auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);

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

		struct GetAudioEncoderConfigurationsHandler : public utility::http::RequestHandlerBase
		{
			GetAudioEncoderConfigurationsHandler() : utility::http::RequestHandlerBase(GetAudioEncoderConfigurations, osrv::auth::SECURITY_LEVELS::READ_MEDIA)
			{}

			OVERLOAD_REQUEST_HANDLER
			{
				pt::ptree ae_configs_node;

				pt::ptree request_xml_tree;
				pt::xml_parser::read_xml(request->content, request_xml_tree);
				
				auto fillAEConfig = [](const pt::ptree& in, pt::ptree& out) {
					out.add("<xmlattr>.token", in.get<std::string>("token"));
					out.add("tt:Name", in.get<std::string>("Name"));
					out.add("tt:UseCount", in.get<int>("UseCount"));
					out.add("tt:Encoding", in.get<std::string>("Encoding"));
					out.add("tt:Bitrate", in.get<int>("Bitrate"));
					out.add("tt:SampleRate", in.get<int>("SampleRate"));
				};

				if (auto requestedToken = exns::find_hierarchy("Envelope.Body.GetAudioEncoderConfigurations.ConfigurationToken", request_xml_tree);
					!requestedToken.empty()) // response with a specific AE config
				{
					auto aeCfg = utility::AudioEncoderReaderByToken(requestedToken, PROFILES_CONFIGS_TREE).AudioEncoder();
					pt::ptree ae_node;
					fillAEConfig(aeCfg, ae_node);
					ae_configs_node.add_child("tr2:Configurations", ae_node);
				}
				else if (auto requestedProfile = exns::find_hierarchy("Envelope.Body.GetAudioEncoderConfigurations.ProfileToken", request_xml_tree);
					!requestedProfile.empty()) // response with a specific profile's AE config
				{
					auto aeCfg = utility::AudioEncoderReaderByProfileToken(requestedProfile, PROFILES_CONFIGS_TREE).AudioEncoder();
					pt::ptree ae_node;
					fillAEConfig(aeCfg, ae_node);
					ae_configs_node.add_child("tr2:Configurations", ae_node);
				}
				else // response with all existing configs
				{
					const auto ae_config_list = PROFILES_CONFIGS_TREE.get_child("AudioEncoderConfigurations");
					for (const auto& ae_config : ae_config_list)
					{
						pt::ptree ae_node;
						fillAEConfig(ae_config.second, ae_node);
						ae_configs_node.add_child("tr2:Configurations", ae_node);
					}
				}
				
				auto env_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);
				env_tree.put_child("s:Body.tr2:GetAudioEncoderConfigurationsResponse", ae_configs_node);
				pt::ptree root_tree;
				root_tree.put_child("s:Envelope", env_tree);

				std::ostringstream os;
				pt::write_xml(os, root_tree);

				utility::http::fillResponseWithHeaders(*response, os.str());
			}
		};

		struct GetAudioEncoderConfigurationOptionsHandler : public utility::http::RequestHandlerBase
		{
			GetAudioEncoderConfigurationOptionsHandler()
				: utility::http::RequestHandlerBase(GetAudioEncoderConfigurationOptions, osrv::auth::SECURITY_LEVELS::READ_MEDIA)
			{}

			OVERLOAD_REQUEST_HANDLER
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
					const auto ae_config_options_list = PROFILES_CONFIGS_TREE.get_child("AudioEncoderConfigurationOptions");
					for (const auto& options : ae_config_options_list)
					{
						pt::ptree options_node;
						fillAEOptions(options.second, options_node);
						ae_opts_node.add_child("tr2:Options", options_node);
					}
				}

				auto env_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);
				env_tree.put_child("s:Body.tr2:GetAudioEncoderConfigurationOptionsResponse", ae_opts_node);
				pt::ptree root_tree;
				root_tree.put_child("s:Envelope", env_tree);

				std::ostringstream os;
				pt::write_xml(os, root_tree);

				utility::http::fillResponseWithHeaders(*response, os.str());
			}
		};

		struct GetAudioDecoderConfigurationsHandler : public utility::http::RequestHandlerBase
		{
			GetAudioDecoderConfigurationsHandler() : utility::http::RequestHandlerBase(GetAudioDecoderConfigurations, osrv::auth::SECURITY_LEVELS::READ_MEDIA)
			{
			}

			OVERLOAD_REQUEST_HANDLER
			{
				pt::ptree ad_configs;
				auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);
				envelope_tree.add_child("s:Body.tr2:GetAudioDecoderConfigurationsResponse", ad_configs);

				pt::ptree root_tree;
				root_tree.put_child("s:Envelope", envelope_tree);

				std::ostringstream os;
				pt::write_xml(os, root_tree);

				utility::http::fillResponseWithHeaders(*response, os.str());
			}
		};

		struct GetProfilesHandler : public utility::http::RequestHandlerBase
		{
			GetProfilesHandler() : utility::http::RequestHandlerBase(GetProfiles, osrv::auth::SECURITY_LEVELS::READ_MEDIA)
			{
			}

			OVERLOAD_REQUEST_HANDLER
			{
				auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);

				auto profiles_configs_list = PROFILES_CONFIGS_TREE.get_child("MediaProfiles");

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

					if (server_configs->multichannel_enabled_)
					{
						std::string vsToken = profiles_configs_list.front().second.get<std::string>("VideoSourceConfiguration");
						for (size_t i = 0; i < server_configs->channels_count_; ++i)
						{
							auto it = profiles_configs_list.begin();
							while (std::find_if(it, profiles_configs_list.end(),
								[vsToken](pt::ptree::value_type tree)
								{
									return tree.second.get<std::string>("VideoSourceConfiguration") == vsToken;
								}) != profiles_configs_list.end())
							{
								pt::ptree profile_node;
								util::profile_to_soap(it->second, PROFILES_CONFIGS_TREE, profile_node);

								std::string profileToken = std::to_string(i) + "_"
									+ it->second.get<std::string>("token");
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
						for (auto elements : profiles_configs_list)
						{
							pt::ptree profile_node;
							util::profile_to_soap(elements.second, PROFILES_CONFIGS_TREE, profile_node);
							response_node.add_child("tr2:Profiles", profile_node);
						}
					}
				}
				else
				{
					// response only one profile's configs

					std::string cleanedName = media::util::MultichannelProfilesNamesConverter(profile_token).CleanedName();

					auto profiles_config_it = std::find_if(profiles_configs_list.begin(),
						profiles_configs_list.end(),
						[&cleanedName](const pt::ptree::value_type& i)
						{
							return i.second.get<std::string>("token") == cleanedName;
						});

					if (profiles_config_it == profiles_configs_list.end())
						throw std::runtime_error("Not found a profile with token: " + profile_token);

					pt::ptree profile_node;
					util::profile_to_soap(profiles_config_it->second, PROFILES_CONFIGS_TREE, profile_node);

					if (server_configs->multichannel_enabled_)
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

		struct GetVideoEncoderConfigurationsHandler : public utility::http::RequestHandlerBase
		{
			GetVideoEncoderConfigurationsHandler() : utility::http::RequestHandlerBase(GetVideoEncoderConfigurations, osrv::auth::SECURITY_LEVELS::READ_MEDIA)
			{
			}

			OVERLOAD_REQUEST_HANDLER
			{
				const auto ve_config_list = PROFILES_CONFIGS_TREE.get_child("VideoEncoderConfigurations2");
				pt::ptree ve_configs_node;
				for (const auto& ve_config : ve_config_list)
				{
					pt::ptree videoencoder_configuration;
					osrv::media2::util::fill_video_encoder(ve_config.second, videoencoder_configuration);
					ve_configs_node.add_child("tr2:Configurations", videoencoder_configuration);
				}

				auto env_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);
				env_tree.put_child("s:Body.tr2:GetVideoEncoderConfigurationsResponse", ve_configs_node);
				pt::ptree root_tree;
				root_tree.put_child("s:Envelope", env_tree);

				std::ostringstream os;
				pt::write_xml(os, root_tree);

				utility::http::fillResponseWithHeaders(*response, os.str());
			}
		};

		struct GetVideoEncoderConfigurationOptionsHandler : public utility::http::RequestHandlerBase
		{
			GetVideoEncoderConfigurationOptionsHandler() : utility::http::RequestHandlerBase(GetVideoEncoderConfigurationOptions, osrv::auth::SECURITY_LEVELS::READ_MEDIA)
			{
			}

			OVERLOAD_REQUEST_HANDLER
			{


				// a request may contain specific config token or profile token,
				// but for now, I no see reason somehow filter and process that additional conditions
				// so just return "as is" from config files all encoding options

				//// extract requested encoder configuration token 
				//std::string configuration_token;
				//std::string profile_token;
				//{
				//	auto request_str = request->content.string();
				//	std::istringstream is(request_str);	
				//	pt::ptree xml_tree;
				//	pt::xml_parser::read_xml(is, xml_tree);
				//	configuration_token = exns::find_hierarchy("Envelope.Body.GetVideoEncoderConfigurationOptions.ConfigurationToken", xml_tree);
				//	profile_token = exns::find_hierarchy("Envelope.Body.GetVideoEncoderConfigurationOptions.ProfileToken", xml_tree);
				//}

				//if (!profile_token.empty())
				//{
				//	const auto profiles_configs_list = PROFILES_CONFIGS_TREE.get_child("MediaProfiles");

				//	auto req_profile_config = std::find_if(profiles_configs_list.begin(), profiles_configs_list.end(),
				//		[profile_token](const pt::ptree::value_type& it)
				//		{
				//			return it.second.get<std::string>("token") == profile_token;
				//		});

				//	if (req_profile_config == profiles_configs_list.end())
				//	{
				//		throw std::runtime_error("Client error: Not found requeried profile token!");
				//	}
				//}



				const auto enc_config_options_list = PROFILES_CONFIGS_TREE.get_child("VideoEncoderConfigurationOptions2");

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
						option_node.add("<xmlattr>.FrameRatesSupported", util::to_value_list(framerates));
					}

					{ //ProfilesSupported 

						std::vector<std::string> profiles;
						auto fps_list = ec.second.get_child("ProfilesSupported");
						for (auto n : fps_list)
						{
							profiles.push_back(n.second.get_value<std::string>());
						}
						option_node.add("<xmlattr>.ProfilesSupported", util::to_value_list(profiles));
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

				auto env_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);
				env_tree.put_child("s:Body.tr2:GetVideoEncoderConfigurationOptionsResponse", response_node);
				pt::ptree root_tree;
				root_tree.put_child("s:Envelope", env_tree);

				std::ostringstream os;
				pt::write_xml(os, root_tree);

				utility::http::fillResponseWithHeaders(*response, os.str());
			}
		};

		struct GetVideoSourceConfigurationsHandler : public utility::http::RequestHandlerBase
		{
			GetVideoSourceConfigurationsHandler() : utility::http::RequestHandlerBase(GetVideoSourceConfigurations, osrv::auth::SECURITY_LEVELS::READ_MEDIA)
			{
			}

			OVERLOAD_REQUEST_HANDLER
			{
				auto vs_config_list = PROFILES_CONFIGS_TREE.get_child("VideoSourceConfigurations");
				pt::ptree vs_configs_node;
				for (const auto& vs_config : vs_config_list)
				{
					pt::ptree videosource_configuration;
					osrv::media::util::fill_soap_videosource_configuration(vs_config.second, videosource_configuration);
					vs_configs_node.put_child("tr2:Configurations", videosource_configuration);

					// multichannel simulaiton with the first channel configs
					if (server_configs->multichannel_enabled_)
					{
						for (size_t i = 0; i < server_configs->channels_count_ - 1; ++i)
						{
							vs_configs_node.put_child("tr2:Configurations", videosource_configuration);
						}

						break;
					}
				}

				auto env_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);
				env_tree.put_child("s:Body.tr2:GetVideoSourceConfigurationsResponse", vs_configs_node);
				pt::ptree root_tree;
				root_tree.put_child("s:Envelope", env_tree);

				std::ostringstream os;
				pt::write_xml(os, root_tree);

				utility::http::fillResponseWithHeaders(*response, os.str());
			}
		};
		
		struct GetVideoSourceConfigurationOptionsHandler : public utility::http::RequestHandlerBase
		{
			GetVideoSourceConfigurationOptionsHandler() : utility::http::RequestHandlerBase(GetVideoSourceConfigurationOptions, osrv::auth::SECURITY_LEVELS::READ_MEDIA)
			{
			}

			OVERLOAD_REQUEST_HANDLER
			{
				// TODO:
				// Here we should parse request and generate a response depends on required profile token and videosource
				// configuration token, but for now it's ignored

				auto vs_config_list = PROFILES_CONFIGS_TREE.get_child("VideoSourceConfigurations");
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

					options_node.put_child("tr2:Options", option);
				}

				auto env_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);
				env_tree.put_child("s:Body.tr2:GetVideoSourceConfigurationOptionsResponse", options_node);
				pt::ptree root_tree;
				root_tree.put_child("s:Envelope", env_tree);

				std::ostringstream os;
				pt::write_xml(os, root_tree);

				utility::http::fillResponseWithHeaders(*response, os.str());
			}
		};
		
		struct GetServiceCapabilitiesHandler : public utility::http::RequestHandlerBase
		{
			GetServiceCapabilitiesHandler() : utility::http::RequestHandlerBase(GetServiceCapabilities, osrv::auth::SECURITY_LEVELS::PRE_AUTH)
			{
			}

			OVERLOAD_REQUEST_HANDLER
			{
				auto capabilities_config = CONFIGS_TREE.get_child("GetServiceCapabilities2");
				pt::ptree capabilities_node;
				capabilities_node.add("<xmlattr>.SnapshotUri", capabilities_config.get<bool>("SnapshotUri"));
				capabilities_node.add("<xmlattr>.Rotation", capabilities_config.get<bool>("Rotation"));
				capabilities_node.add("<xmlattr>.VideoSourceMode", capabilities_config.get<bool>("VideoSourceMode"));
				capabilities_node.add("<xmlattr>.OSD", capabilities_config.get<bool>("OSD"));
				capabilities_node.add("<xmlattr>.TemporaryOSDText", capabilities_config.get<bool>("TemporaryOSDText"));
				capabilities_node.add("<xmlattr>.Mask", capabilities_config.get<bool>("Mask"));
				capabilities_node.add("<xmlattr>.SourceMask", capabilities_config.get<bool>("SourceMask"));
				
				capabilities_node.add("tr2:ProfileCapabilities.MaximumNumberOfProfiles", capabilities_config.get<int>("ProfileCapabilities.MaximumNumberOfProfiles"));
				capabilities_node.add("tr2:ProfileCapabilities.ConfigurationsSupported", capabilities_config.get<std::string>("ProfileCapabilities.ConfigurationsSupported"));

				capabilities_node.add("tr2:StreamingCapabilities.<xmlattr>.RTSPStreaming", capabilities_config.get<bool>("StreamingCapabilities.RTSPStreaming"));
				capabilities_node.add("tr2:StreamingCapabilities.<xmlattr>.RTPMulticast", capabilities_config.get<bool>("StreamingCapabilities.RTPMulticast"));
				capabilities_node.add("tr2:StreamingCapabilities.<xmlattr>.RTP_RTSP_TCP", capabilities_config.get<bool>("StreamingCapabilities.RTP_RTSP_TCP"));
				capabilities_node.add("tr2:StreamingCapabilities.<xmlattr>.NonAggregateControl", capabilities_config.get<bool>("StreamingCapabilities.NonAggregateControl"));
				capabilities_node.add("tr2:StreamingCapabilities.<xmlattr>.AutoStartMulticast", capabilities_config.get<bool>("StreamingCapabilities.AutoStartMulticast"));

				auto env_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);
				env_tree.put_child("s:Body.tr2:GetServiceCapabilitiesResponse", capabilities_node);
				pt::ptree root_tree;
				root_tree.put_child("s:Envelope", env_tree);

				std::ostringstream os;
				pt::write_xml(os, root_tree);

				utility::http::fillResponseWithHeaders(*response, os.str());
			}
		};

		struct GetStreamUriHandler : public utility::http::RequestHandlerBase
		{
			GetStreamUriHandler() : utility::http::RequestHandlerBase(GetStreamUri,
				osrv::auth::SECURITY_LEVELS::READ_MEDIA)
			{
			}

			OVERLOAD_REQUEST_HANDLER
			{
				pt::ptree request_xml;
				pt::xml_parser::read_xml(std::istringstream{ request->content.string() }, request_xml);

				std::string requested_token = exns::find_hierarchy("Envelope.Body.GetStreamUri.ProfileToken", request_xml);

				if (server_configs->multichannel_enabled_)
				{
					requested_token = media::util::MultichannelProfilesNamesConverter(requested_token).CleanedName();
				}

				logger_->Debug("Requested token to get URI=" + requested_token);

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
				auto rtsp_url = util::generate_rtsp_url(*server_configs, stream_config_it->second.get<std::string>("Uri"));
				//std::string rtsp_url;
				response_node.put("tr2:Uri", rtsp_url);

				auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);
				envelope_tree.put_child("s:Body.tr2:GetStreamUriResponse", response_node);

				pt::ptree root_tree;
				root_tree.put_child("s:Envelope", envelope_tree);

				std::ostringstream os;
				pt::write_xml(os, root_tree);

				utility::http::fillResponseWithHeaders(*response, os.str());
			}
		};
		
		struct SetVideoEncoderConfigurationHandler : public utility::http::RequestHandlerBase
		{
			SetVideoEncoderConfigurationHandler() : utility::http::RequestHandlerBase(SetVideoEncoderConfiguration,
				osrv::auth::SECURITY_LEVELS::ACTUATE)
			{
			}

			OVERLOAD_REQUEST_HANDLER
			{
				pt::ptree request_xml;
				pt::xml_parser::read_xml(std::istringstream{ request->content.string() }, request_xml);

				// TODO: add impmlementation

				auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);
				envelope_tree.put("s:Body.tr2:SetVideoEncoderConfigurationResponse", "");

				pt::ptree root_tree;
				root_tree.put_child("s:Envelope", envelope_tree);

				std::ostringstream os;
				pt::write_xml(os, root_tree);

				utility::http::fillResponseWithHeaders(*response, os.str());
			}
		};
		
		struct SetVideoSourceConfigurationHandler : public utility::http::RequestHandlerBase
		{
			SetVideoSourceConfigurationHandler() : utility::http::RequestHandlerBase(SetVideoSourceConfiguration,
				osrv::auth::SECURITY_LEVELS::ACTUATE)
			{
			}

			OVERLOAD_REQUEST_HANDLER
			{
				pt::ptree request_xml;
				pt::xml_parser::read_xml(std::istringstream{ request->content.string() }, request_xml);

				// TODO: add implementation

				auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);
				envelope_tree.put("s:Body.tr2:SetVideoSourceConfigurationResponse", "");

				pt::ptree root_tree;
				root_tree.put_child("s:Envelope", envelope_tree);

				std::ostringstream os;
				pt::write_xml(os, root_tree);

				utility::http::fillResponseWithHeaders(*response, os.str());
			}
		};

        //DEFAULT HANDLER
        void Media2ServiceHandler(std::shared_ptr<HttpServer::Response> response,
            std::shared_ptr<HttpServer::Request> request)
        {
			if (auto delay = server_configs->network_delay_simulation_; delay > 0)
			{
				auto timer = std::make_shared<boost::asio::deadline_timer>(*server_configs->io_context_,
					boost::posix_time::milliseconds(delay));
				timer->async_wait(
					[timer, response, request](const boost::system::error_code& ec)
					{
						if (ec)
							return;

						do_handler_request(response, request);
					}
				);

			}
			else
			{
				do_handler_request(response, request);
			}
        }

		void do_handler_request(std::shared_ptr<HttpServer::Response> response,
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
				logger_->Error(e.what());
			}
			auto handler_it = std::find_if(handlers.begin(), handlers.end(),
				[&method](const utility::http::HandlerSP handler) {
					return handler->get_name() == method;
				});

			//handle requests
			if (handler_it != handlers.end())
			{
				//TODO: Refactor and take out to general place this authentication logic
				//check user credentials
				try
				{
					auto handler_ptr = *handler_it;
					logger_->Debug("Handling Media2Service request: " + handler_ptr->get_name());

					//extract user credentials
					osrv::auth::USER_TYPE current_user = osrv::auth::USER_TYPE::ANON;
					if (server_configs->auth_scheme_ == AUTH_SCHEME::DIGEST)
					{
						auto auth_header_it = request->header.find(utility::http::HEADER_AUTHORIZATION);
						if (auth_header_it != request->header.end())
						{
							//do extract user creds
							auto da_from_request = utility::digest::extract_DA(auth_header_it->second);

							bool isStaled;
							auto isCredsOk = digest_session->verifyDigest(da_from_request, isStaled);

							//if provided credentials are OK, upgrade UserType from Anon to appropriate Type
							if (isCredsOk)
							{
								current_user = osrv::auth::get_usertype_by_username(da_from_request.username, digest_session->get_users_list());
							}
						}
					
						if (!osrv::auth::isUserHasAccess(current_user, handler_ptr->get_security_level()))
						{
							throw osrv::auth::digest_failed{};
						}
					}

					(*handler_ptr)(response, request);
				}
				catch (const osrv::auth::digest_failed& e)
				{
					logger_->Error(e.what());
					
					*response << utility::http::RESPONSE_UNAUTHORIZED << "\r\n"
						<< "Content-Type: application/soap+xml; charset=utf-8" << "\r\n"
						<< "Content-Length: " << 0 << "\r\n"
						<< utility::http::HEADER_WWW_AUTHORIZATION << ": " << digest_session->generateDigest().to_string() << "\r\n"
						<< "\r\n";
				}
				catch (const std::exception& e)
				{
					logger_->Error("A server's error occured in Media2Service while processing: " + method
						+ ". What: " + e.what());
					
					*response << "HTTP/1.1 500 Server error\r\nContent-Length: " << 0 << "\r\n\r\n";
				}
			}
			else
			{
				logger_->Error("Not found an appropriate handler in Media2Service for: " + method);
				*response << "HTTP/1.1 400 Bad request\r\nContent-Length: " << 0 << "\r\n\r\n";
			}
		}

        void init_service(HttpServer& srv, const osrv::ServerConfigs& server_configs_ptr,
			const std::string& configs_path, ILogger& logger)
        {
            if (logger_ != nullptr)
                return logger_->Error("Media2Service is already initiated!");

            logger_ = &logger;

            logger_->Debug("Initiating Media2 service...");

			server_configs = &server_configs_ptr;
			digest_session = server_configs_ptr.digest_session_;

            pt::read_json(configs_path + MEDIA_SERVICE_CONFIGS_PATH, CONFIGS_TREE);
            pt::read_json(configs_path + PROFILES_CONFIGS_PATH, PROFILES_CONFIGS_TREE);

            auto namespaces_tree = CONFIGS_TREE.get_child("Namespaces");
            for (const auto& n : namespaces_tree)
                XML_NAMESPACES.insert({ n.first, n.second.get_value<std::string>() });

			handlers.emplace_back(new GetAnalyticsConfigurationsHandler());
			handlers.emplace_back(new GetAudioDecoderConfigurationsHandler());
			handlers.emplace_back(new GetAudioEncoderConfigurationsHandler());
			handlers.emplace_back(new GetAudioEncoderConfigurationOptionsHandler());
			handlers.emplace_back(new GetProfilesHandler());
			handlers.emplace_back(new GetServiceCapabilitiesHandler());
			handlers.emplace_back(new GetVideoEncoderConfigurationsHandler());
			handlers.emplace_back(new GetVideoEncoderConfigurationOptionsHandler());
			handlers.emplace_back(new GetVideoSourceConfigurationsHandler());
			handlers.emplace_back(new GetVideoSourceConfigurationOptionsHandler());
			handlers.emplace_back(new GetStreamUriHandler());
			handlers.emplace_back(new SetVideoEncoderConfigurationHandler());
			handlers.emplace_back(new SetVideoSourceConfigurationHandler());

            srv.resource["/onvif/media2_service"]["POST"] = Media2ServiceHandler;
        }


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

				{ // Videoanalytics
					// just fill dummy configs

					pt::ptree analytics_node;
					osrv::media::util::fill_analytics_configuration(analytics_node);
					result.put_child("tr2:Configurations.tr2:Analytics", analytics_node);
				}

				{ // PTZ
					// TODO: make reading from a profile

					pt::ptree ptz_node;
					{
						ptz_node.add("<xmlattr>.token", "PtzConfigToken0");
						ptz_node.add("tt:Name", "PtzConfig0");
						ptz_node.add("tt:UseCount", 3);
						ptz_node.add("tt:NodeToken", "PTZNODE_1");
						ptz_node.add("tt:DefaultContinuousPanTiltVelocitySpace", 
							"http://www.onvif.org/ver10/tptz/PanTiltSpaces/VelocityGenericSpace");
						ptz_node.add("tt:DefaultContinuousZoomVelocitySpace",
							"http://www.onvif.org/ver10/tptz/ZoomSpaces/VelocityGenericSpace");
					}
					result.put_child("tr2:Configurations.tr2:PTZ", ptz_node);
				}

				{ // Audio source
					auto as_token = profile_config.get<std::string>("AudioSourceConfiguration", "");
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
				}

				{ // Audio encoder
					auto ae_token = profile_config.get<std::string>("AudioEncoderConfiguration", "");
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
				videoencoder_node.add("tt:RateControl.tt:ConstantBitRate", config_node.get<bool>("RateControl.ConstantBitRate"));
				videoencoder_node.add("tt:RateControl.tt:FrameRateLimit", config_node.get<float>("RateControl.FrameRateLimit"));
				videoencoder_node.add("tt:RateControl.tt:BitrateLimit", config_node.get<int>("RateControl.BitrateLimit"));
			}
			
		}
    }
}
