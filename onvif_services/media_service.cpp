#include "media_service.h"

#include "../Logger.h"
#include "../utility/XmlParser.h"
#include "../utility/HttpHelper.h"
#include "../utility/SoapHelper.h"
#include "../Server.h"

#include "../Simple-Web-Server/server_http.hpp"

#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <algorithm>

static const osrv::ServerConfigs* server_configs;
static std::shared_ptr<utility::digest::IDigestSession> digest_session;

static ILogger* logger_ = nullptr;


static const std::string PROFILES_CONFIGS_PATH = "media_profiles.config";
static const std::string MEDIA_SERVICE_CONFIGS_PATH = "media.config";

namespace pt = boost::property_tree;
static pt::ptree CONFIGS_TREE;
static pt::ptree PROFILES_CONFIGS_TREE;
static std::map<std::string, std::string> XML_NAMESPACES;

//the list of implemented methods
static const std::string GetAudioDecoderConfigurations = "GetAudioDecoderConfigurations";
static const std::string GetAudioOutputs = "GetAudioOutputs";
static const std::string GetAudioSourceConfigurations = "GetAudioSourceConfigurations";
static const std::string GetAudioSources = "GetAudioSources";
static const std::string GetProfile = "GetProfile";
static const std::string GetProfiles = "GetProfiles";
static const std::string GetVideoAnalyticsConfigurations = "GetVideoAnalyticsConfigurations";
static const std::string GetVideoSourceConfiguration = "GetVideoSourceConfiguration";
static const std::string GetVideoSourceConfigurations = "GetVideoSourceConfigurations";
static const std::string GetVideoSources = "GetVideoSources";
static const std::string GetStreamUri = "GetStreamUri";

//soap helper functions
void fill_soap_media_profile(const pt::ptree& /*in_json_config*/, pt::ptree& /*out_profile_node*/,
	const std::string& /*root_node_value*/);

namespace osrv
{
	namespace media
	{
		//TODO:: Need release
		static std::vector<utility::http::HandlerSP> handlers;

		void do_handler_request(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request);

		struct GetAudioDecoderConfigurationsHandler : public utility::http::RequestHandlerBase
		{
			GetAudioDecoderConfigurationsHandler() : utility::http::RequestHandlerBase(GetAudioDecoderConfigurations, osrv::auth::SECURITY_LEVELS::READ_MEDIA)
			{
			}

			OVERLOAD_REQUEST_HANDLER
			{
				pt::ptree ad_configs;
				auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);
				envelope_tree.add_child("s:Body.trt:GetAudioDecoderConfigurationsResponse", ad_configs);

				pt::ptree root_tree;
				root_tree.put_child("s:Envelope", envelope_tree);

				std::ostringstream os;
				pt::write_xml(os, root_tree);

				utility::http::fillResponseWithHeaders(*response, os.str());
			}
		};

		struct GetAudioOutputsHandler : public utility::http::RequestHandlerBase
		{
			GetAudioOutputsHandler() : utility::http::RequestHandlerBase(GetAudioOutputs,
				osrv::auth::SECURITY_LEVELS::READ_MEDIA)
			{
			}

			OVERLOAD_REQUEST_HANDLER
			{
				pt::ptree aoutputs;
				auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);
				envelope_tree.add_child("s:Body.trt:GetAudioOutputsResponse", aoutputs);

				pt::ptree root_tree;
				root_tree.put_child("s:Envelope", envelope_tree);

				std::ostringstream os;
				pt::write_xml(os, root_tree);

				utility::http::fillResponseWithHeaders(*response, os.str());
			}
		};

		struct GetAudioSourceConfigurationsHandler : public utility::http::RequestHandlerBase
		{
			GetAudioSourceConfigurationsHandler() : utility::http::RequestHandlerBase(GetAudioSourceConfigurations,
				osrv::auth::SECURITY_LEVELS::READ_MEDIA)
			{
			}

			OVERLOAD_REQUEST_HANDLER
			{
				pt::ptree as_configs;
				auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);
				envelope_tree.add_child("s:Body.trt:GetAudioSourceConfigurationsResponse", as_configs);

				pt::ptree root_tree;
				root_tree.put_child("s:Envelope", envelope_tree);

				std::ostringstream os;
				pt::write_xml(os, root_tree);

				utility::http::fillResponseWithHeaders(*response, os.str());
			}
		};

		struct GetAudioSourcesHandler : public utility::http::RequestHandlerBase
		{
			GetAudioSourcesHandler() : utility::http::RequestHandlerBase(GetAudioSources,
				osrv::auth::SECURITY_LEVELS::READ_MEDIA)
			{
			}

			OVERLOAD_REQUEST_HANDLER
			{
				pt::ptree asources;
				auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);
				envelope_tree.add_child("s:Body.trt:GetAudioSourcesResponse", asources);

				pt::ptree root_tree;
				root_tree.put_child("s:Envelope", envelope_tree);

				std::ostringstream os;
				pt::write_xml(os, root_tree);

				utility::http::fillResponseWithHeaders(*response, os.str());
			}
		};

		struct GetProfileHandler : public utility::http::RequestHandlerBase
		{
			GetProfileHandler() : utility::http::RequestHandlerBase(GetProfile,
				osrv::auth::SECURITY_LEVELS::READ_MEDIA)
			{
			}

			OVERLOAD_REQUEST_HANDLER
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

				std::string cleaned_token = util::MultichannelProfilesNamesConverter(requested_token).CleanedName();

				auto profiles_config_list = PROFILES_CONFIGS_TREE.get_child("MediaProfiles");
				auto profile_config = std::find_if(profiles_config_list.begin(), profiles_config_list.end(),
					[cleaned_token](pt::ptree::value_type vs_obj)
					{
						return vs_obj.second.get<std::string>("token") == cleaned_token;
					});

				if (profile_config == profiles_config_list.end())
					throw std::runtime_error("The requested profile token ProfileToken does not exist");

				pt::ptree profile_node;
				fill_soap_media_profile(profile_config->second, profile_node, "trt:Profile");

				if (server_configs->multichannel_enabled_)
				{
					profile_node.put("<xmlattr>.token", requested_token);
				}

				auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);
				envelope_tree.put_child("s:Body.trt:GetProfileResponse.trt:Profile", profile_node);

				pt::ptree root_tree;
				root_tree.put_child("s:Envelope", envelope_tree);

				std::ostringstream os;
				pt::write_xml(os, root_tree);

				utility::http::fillResponseWithHeaders(*response, os.str());
			}
		};

		struct GetProfilesHandler : public utility::http::RequestHandlerBase
		{
			GetProfilesHandler() : utility::http::RequestHandlerBase(GetProfiles,
				osrv::auth::SECURITY_LEVELS::READ_MEDIA)
			{
			}

			OVERLOAD_REQUEST_HANDLER
			{
				auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);

				auto profiles_config = PROFILES_CONFIGS_TREE.get_child("MediaProfiles");
				pt::ptree response_node;

				if (server_configs->multichannel_enabled_)
				{
					std::string vsToken = profiles_config.front().second.get<std::string>("VideoSourceConfiguration");
					for (size_t i = 0; i < server_configs->channels_count_; ++i)
					{
						auto it = profiles_config.begin();
						while (std::find_if(it, profiles_config.end(),
							[vsToken](pt::ptree::value_type tree)
							{
								return tree.second.get<std::string>("VideoSourceConfiguration") == vsToken;
							}) != profiles_config.end())
						{
							pt::ptree profile_node;
							fill_soap_media_profile(it->second, profile_node, "");

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
						fill_soap_media_profile(elements.second, profile_node, "");

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
		};

		struct GetVideoAnalyticsConfigurationsHandler : public utility::http::RequestHandlerBase
		{
			GetVideoAnalyticsConfigurationsHandler() : utility::http::RequestHandlerBase(GetVideoAnalyticsConfigurations,
				osrv::auth::SECURITY_LEVELS::READ_MEDIA)
			{
			}

			OVERLOAD_REQUEST_HANDLER
			{
				pt::ptree analytics_configs;
				auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);
				envelope_tree.add_child("s:Body.trt:GetVideoAnalyticsConfigurationsResponse.trt:Configurations", analytics_configs);

				pt::ptree root_tree;
				root_tree.put_child("s:Envelope", envelope_tree);

				std::ostringstream os;
				pt::write_xml(os, root_tree);

				utility::http::fillResponseWithHeaders(*response, os.str());
			}
		};

		struct GetVideoSourceConfigurationHandler : public utility::http::RequestHandlerBase
		{
			GetVideoSourceConfigurationHandler() : utility::http::RequestHandlerBase(GetVideoSourceConfiguration,
				osrv::auth::SECURITY_LEVELS::READ_MEDIA)
			{
			}

			OVERLOAD_REQUEST_HANDLER
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

				auto vs_config_list = PROFILES_CONFIGS_TREE.get_child("VideoSourceConfigurations");

				auto vs_config = std::find_if(vs_config_list.begin(), vs_config_list.end(),
					[requested_token](pt::ptree::value_type vs_obj)
					{
						return vs_obj.second.get<std::string>("token") == requested_token;
					});

				if (vs_config == vs_config_list.end())
					throw std::runtime_error("The requested configuration indicated with ConfigurationToken does not exist.");

				pt::ptree videosource_configuration_node;
				util::fill_soap_videosource_configuration(vs_config->second, videosource_configuration_node);

				auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);
				envelope_tree.add_child("s:Body.trt:GetVideoSourceConfigurationResponse.trt:Configuration", videosource_configuration_node);

				pt::ptree root_tree;
				root_tree.put_child("s:Envelope", envelope_tree);

				std::ostringstream os;
				pt::write_xml(os, root_tree);

				utility::http::fillResponseWithHeaders(*response, os.str());
			}
		};

		struct GetVideoSourceConfigurationsHandler : public utility::http::RequestHandlerBase
		{
			GetVideoSourceConfigurationsHandler() : utility::http::RequestHandlerBase(GetVideoSourceConfigurations,
				osrv::auth::SECURITY_LEVELS::READ_MEDIA)
			{
			}

			OVERLOAD_REQUEST_HANDLER
			{
				auto vs_config_list = PROFILES_CONFIGS_TREE.get_child("VideoSourceConfigurations");

				pt::ptree vs_configs_node;
				for (const auto& vs_config : vs_config_list)
				{
					pt::ptree vs_config_node;
					util::fill_soap_videosource_configuration(vs_config.second, vs_config_node);
					vs_configs_node.add_child("trt:Configurations", vs_config_node);
				}

				auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);
				envelope_tree.add_child("s:Body.trt:GetVideoSourceConfigurationsResponse", vs_configs_node);

				pt::ptree root_tree;
				root_tree.put_child("s:Envelope", envelope_tree);

				std::ostringstream os;
				pt::write_xml(os, root_tree);

				utility::http::fillResponseWithHeaders(*response, os.str());
			}
		};

		struct GetVideoSourcesHandler : public utility::http::RequestHandlerBase
		{
			GetVideoSourcesHandler() : utility::http::RequestHandlerBase(GetVideoSources,
				osrv::auth::SECURITY_LEVELS::READ_MEDIA)
			{
			}

			OVERLOAD_REQUEST_HANDLER
			{
				auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);

				auto videosources_config = CONFIGS_TREE.get_child(GetVideoSources);
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
					if (server_configs->multichannel_enabled_)
					{
						for (size_t i = 1; i < server_configs->channels_count_; ++i)
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

				//TODO: add way to search for child with full path like: "Envelope.Body.GetPr..."
				std::string requested_token;
				{
					auto envelope_node = exns::find("Envelope", request_xml);
					auto body_node = exns::find("Body", envelope_node->second);
					auto profile_node = exns::find("GetStreamUri", body_node->second);
					auto profile_token = exns::find("ProfileToken", profile_node->second);
					requested_token = profile_token->second.get_value<std::string>();

					logger_->Debug("Requested token to get URI: " + requested_token);
				}

				if (server_configs->multichannel_enabled_)
				{
					requested_token = util::MultichannelProfilesNamesConverter(requested_token).CleanedName();
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

				auto rtsp_url = util::generate_rtsp_url(*server_configs, stream_config_it->second.get<std::string>("Uri"));
				response_node.put("trt:MediaUri.tt:Uri",
					rtsp_url);
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
		};

		//DEFAULT HANDLER
		void MediaServiceHandler(std::shared_ptr<HttpServer::Response> response,
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
					logger_->Debug("Handling DeviceService request: " + handler_ptr->get_name());

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
					logger_->Error("A server's error occured in DeviceService while processing: " + method
						+ ". Info: " + e.what());

					*response << "HTTP/1.1 500 Server error\r\nContent-Length: " << 0 << "\r\n\r\n";
				}
			}
			else
			{
				logger_->Error("Not found an appropriate handler in DeviceService for: " + method);
				*response << "HTTP/1.1 400 Bad request\r\nContent-Length: " << 0 << "\r\n\r\n";
			}
		}

		void init_service(HttpServer& srv, const osrv::ServerConfigs& server_configs_ptr, const std::string& configs_path, ILogger& logger)
		{
			if (logger_ != nullptr)
				return logger_->Error("MediaService is already initiated!");

			logger_ = &logger;

			logger_->Debug("Initiating Media service...");

			server_configs = &server_configs_ptr;
			digest_session = server_configs_ptr.digest_session_;

			pt::read_json(configs_path + MEDIA_SERVICE_CONFIGS_PATH, CONFIGS_TREE);
			pt::read_json(configs_path + PROFILES_CONFIGS_PATH, PROFILES_CONFIGS_TREE);

			auto namespaces_tree = CONFIGS_TREE.get_child("Namespaces");
			for (const auto& n : namespaces_tree)
				XML_NAMESPACES.insert({ n.first, n.second.get_value<std::string>() });

			handlers.emplace_back(new GetAudioDecoderConfigurationsHandler);
			handlers.emplace_back(new GetAudioOutputsHandler);
			handlers.emplace_back(new GetAudioSourceConfigurationsHandler);
			handlers.emplace_back(new GetAudioSourcesHandler);
			handlers.emplace_back(new GetProfileHandler);
			handlers.emplace_back(new GetProfilesHandler);
			handlers.emplace_back(new GetVideoAnalyticsConfigurationsHandler);
			handlers.emplace_back(new GetVideoSourceConfigurationHandler);
			handlers.emplace_back(new GetVideoSourceConfigurationsHandler);
			handlers.emplace_back(new GetVideoSourcesHandler);
			handlers.emplace_back(new GetStreamUriHandler);

			srv.resource["/onvif/media_service"]["POST"] = MediaServiceHandler;
		}
	}
}

void fill_soap_media_profile(const pt::ptree& json_config, pt::ptree& profile_node,
	const std::string& root_node_value)
{
	profile_node.put("<xmlattr>.token", json_config.get<std::string>("token"));
	profile_node.put("<xmlattr>.fixed", json_config.get<std::string>("fixed"));
	profile_node.put("tt:Name", json_config.get<std::string>("Name"));

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

		pt::ptree videosource_configuration;
		osrv::media::util::fill_soap_videosource_configuration(vs_config->second, videosource_configuration);
		profile_node.put_child("tt:VideoSourceConfiguration", videosource_configuration);
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
				ratecontrol_config_it->second.get<std::string>("GuaranteedFrameRate"));
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

		profile_node.put("tt:VideoEncoderConfiguration.tt:SessionTimeout",
			ve_config->second.get<std::string>("SessionTimeout"));
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

std::string osrv::media::util::generate_rtsp_url(const ServerConfigs& server_configs, const std::string& profile_stream_url)
{
	std::stringstream rtsp_url;
	rtsp_url << "rtsp://" << server_configs.ipv4_address_ << ":"
		<< (server_configs.enabled_rtsp_port_forwarding ? std::to_string(server_configs.forwarded_rtsp_port)
			: server_configs.rtsp_port_)
		<< "/" << profile_stream_url;

	return rtsp_url.str();
}
