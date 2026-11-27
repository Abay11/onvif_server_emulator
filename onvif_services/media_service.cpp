#include "../utility/StreamTransport.h"
#include "metadata_handlers.h"
#include "../utility/MetadataConfiguration.h"
#include "analytics_service.h"
#include "media_service.h"
#include "ptz_service.h"
#include "../utility/ChannelTopology.h"

#include "../onvif/OnvifRequest.h"

#include "../Logger.h"
#include "../Server.h"
#include "../utility/AudioSourceReader.h"
#include "../utility/HttpHelper.h"
#include "../utility/MediaProfilesManager.h"
#include "../utility/SoapHelper.h"
#include "../utility/XmlParser.h"
#include "media2_service.h"

#include "../HttpServer.h"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <algorithm>
#include <format>

namespace pt = boost::property_tree;

// the list of implemented methods
static const std::string GetAudioDecoderConfigurations = "GetAudioDecoderConfigurations";
static const std::string GetAudioEncoderConfiguration = "GetAudioEncoderConfiguration";
static const std::string GetAudioEncoderConfigurationOptions = "GetAudioEncoderConfigurationOptions";
static const std::string GetAudioOutputs = "GetAudioOutputs";
static const std::string GetAudioSources = "GetAudioSources";
static const std::string GetProfile = "GetProfile";
static const std::string GetProfiles = "GetProfiles";
static const std::string GetSnapshotUri = "GetSnapshotUri";
static const std::string GetStreamUri = "GetStreamUri";
static const std::string GetVideoAnalyticsConfigurations = "GetVideoAnalyticsConfigurations";
static const std::string GetVideoSourceConfiguration = "GetVideoSourceConfiguration";
static const std::string GetVideoSourceConfigurations = "GetVideoSourceConfigurations";
static const std::string GetVideoSources = "GetVideoSources";

// soap helper functions
void fill_soap_media_profile(const pt::ptree& profile, pt::ptree& result,
														 const utility::media::MediaProfilesManager& media);

namespace osrv
{
namespace media
{
struct GetAudioDecoderConfigurationsHandler : public OnvifRequestBase
{
	GetAudioDecoderConfigurationsHandler(const std::map<std::string, std::string>& xs,
																			 const std::shared_ptr<pt::ptree>& configs)
			: OnvifRequestBase(GetAudioDecoderConfigurations, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
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
	GetAudioEncoderConfigurationOptionsHandler(const std::map<std::string, std::string>& xs,
																						 const std::shared_ptr<pt::ptree>& configs,
																						 const std::shared_ptr<pt::ptree>& profiles_configs)
			: OnvifRequestBase(GetAudioEncoderConfigurationOptions, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs),
				profiles_configs_(profiles_configs)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		pt::ptree options_tree;

		auto fillAEOptions = [](const pt::ptree& in, pt::ptree& out) {
			// NOTE: IN MEDIA1 there is only G711 and AAC encoding
			// so we need correct name here for PCMU and MP4A-LATM.
			// PCMA I just skip
			auto encoding = in.get<std::string>("Encoding");
			if (encoding == "PCMA")
				return;
			if (encoding == "PCMU")
				encoding = "G711";
			if (encoding == "MP4A-LATM")
				encoding = "AAC";

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
			: OnvifRequestBase(GetAudioEncoderConfiguration, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs),
				profiles_configs_(profiles_configs)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		pt::ptree request_xml_tree;
		pt::xml_parser::read_xml(request->content, request_xml_tree);
		auto requested_config_token =
				exns::find_hierarchy("GetAudioEncoderConfiguration.ConfigurationToken", request_xml_tree);

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
	GetAudioOutputsHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs)
			: OnvifRequestBase(GetAudioOutputs, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
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



struct GetAudioSourcesHandler : public OnvifRequestBase
{
	GetAudioSourcesHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs,
												 const std::shared_ptr<pt::ptree>& profiles_configs)
			: OnvifRequestBase(GetAudioSources, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs),
				profiles_configs_(profiles_configs)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		pt::ptree asources;

		for (const auto& [_, input] : profiles_configs_->get_child("AudioSources"))
		{
			pt::ptree source;
			source.put("<xmlattr>.token", input.get<std::string>("Token"));
			source.put("tt:Channels", 1);
			asources.add_child("trt:AudioSources", source);
		}

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



struct ChannelConfigurationHandler : OnvifRequestBase
{
	ChannelConfigurationHandler(const std::string& operation, auth::SECURITY_LEVELS level,
															const std::map<std::string, std::string>& ns,
															const std::shared_ptr<pt::ptree>& config,
															utility::media::MediaProfilesManager& manager,
															std::string type, std::string element)
		: OnvifRequestBase(operation, level, ns, config), manager_(manager),
			type_(std::move(type)), element_(std::move(element))
	{
	}
	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		pt::ptree xml, result;
		pt::read_xml(request->content, xml);
		const auto path = "Envelope.Body." + name();
		const auto profileToken = exns::find_hierarchy(path + ".ProfileToken", xml);
		if (name().starts_with("Add"))
			manager_.AddConfiguration(profileToken, type_, exns::find_hierarchy(path + ".ConfigurationToken", xml));
		else if (name().starts_with("Remove"))
			manager_.RemoveConfiguration(profileToken, type_);
		else
		{
			const auto* profile = name().starts_with("GetCompatible") ? &manager_.GetProfileByToken(profileToken) : nullptr;
			const auto& tree = manager_.ReaderWriter()->ConfigsTree();
			const pt::ptree empty;
			for (const auto& [_, candidate] : tree.get_child(type_, empty))
			{
				const auto token = candidate.get<std::string>("token");
				if (profile && !manager_.Compatible(*profile, type_, token))
					continue;
				pt::ptree projection, configuration;
				projection.put("token", "");
				projection.put("fixed", false);
				projection.put("Name", "");
				projection.put(type_, token);
				fill_soap_media_profile(projection, configuration, manager_);
				auto& child = configuration.get_child("tt:" + element_ + "Configuration");
				child.put("tt:UseCount", manager_.GetUseCount(token, type_));
				result.add_child("trt:Configurations", child);
			}
		}
		auto envelope = utility::soap::getEnvelopeTree(ns_);
		envelope.put_child("s:Body.trt:" + name() + "Response", result);
		pt::ptree root;
		root.put_child("s:Envelope", envelope);
		std::ostringstream output;
		pt::write_xml(output, root);
		utility::http::fillResponseWithHeaders(*response, output.str());
	}

private:
	utility::media::MediaProfilesManager& manager_;
	std::string type_, element_;
};

struct GetProfileHandler : public OnvifRequestBase
{
	GetProfileHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs,
										const utility::media::MediaProfilesManager& manager)
			: OnvifRequestBase(GetProfile, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs), manager_(manager)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		pt::ptree request_xml;
		std::istringstream is(request->content.string());
		pt::xml_parser::read_xml(is, request_xml);

		const auto token = exns::find_hierarchy("Envelope.Body.GetProfile.ProfileToken", request_xml);
		pt::ptree profile_node;
		fill_soap_media_profile(manager_.GetProfileByToken(token), profile_node, manager_);

		auto envelope_tree = utility::soap::getEnvelopeTree(ns_);
		envelope_tree.put_child("s:Body.trt:GetProfileResponse.trt:Profile", profile_node);

		pt::ptree root_tree;
		root_tree.put_child("s:Envelope", envelope_tree);

		std::ostringstream os;
		pt::write_xml(os, root_tree);

		utility::http::fillResponseWithHeaders(*response, os.str());
	}

private:
	const utility::media::MediaProfilesManager& manager_;
};

struct GetProfilesHandler : public OnvifRequestBase
{
	GetProfilesHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs,
										 const utility::media::MediaProfilesManager& manager)
			: OnvifRequestBase(GetProfiles, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs), manager_(manager)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		auto envelope_tree = utility::soap::getEnvelopeTree(ns_);

		auto profiles_config = manager_.ReaderWriter()->ConfigsTree().get_child("MediaProfiles");
		pt::ptree response_node;

		for (const auto& [key, tree] : profiles_config)
		{
			pt::ptree profile_node;
			fill_soap_media_profile(tree, profile_node, manager_);
			response_node.add_child("trt:Profiles", profile_node);
		}

		envelope_tree.put_child("s:Body.trt:GetProfilesResponse", response_node);

		pt::ptree root_tree;
		root_tree.put_child("s:Envelope", envelope_tree);

		std::ostringstream os;
		pt::write_xml(os, root_tree);

		utility::http::fillResponseWithHeaders(*response, os.str());
	}

private:
	const utility::media::MediaProfilesManager& manager_;
};

struct GetVideoAnalyticsConfigurationsHandler : public OnvifRequestBase
{
	GetVideoAnalyticsConfigurationsHandler(const std::map<std::string, std::string>& xs,
																				 const std::shared_ptr<pt::ptree>& configs, const utility::media::MediaProfilesManager& mgr)
			: OnvifRequestBase(GetVideoAnalyticsConfigurations, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs), manager(mgr)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{

		auto envelope_tree = utility::soap::getEnvelopeTree(ns_);
		envelope_tree.put("s:Body.trt:GetVideoAnalyticsConfigurationsResponse", "");
		for (const auto& [_, cfg] : manager.ReaderWriter()->ConfigsTree().get_child("Analytics"))
		{
			pt::ptree out;
			analytics::fillConfiguration(cfg, out, manager);
			envelope_tree.add_child("s:Body.trt:GetVideoAnalyticsConfigurationsResponse.trt:Configurations", out);
		}

		pt::ptree root_tree;
		root_tree.put_child("s:Envelope", envelope_tree);

		std::ostringstream os;
		pt::write_xml(os, root_tree);

		utility::http::fillResponseWithHeaders(*response, os.str());
	}
	const utility::media::MediaProfilesManager& manager;
};

struct GetVideoSourceConfigurationHandler : public OnvifRequestBase
{
	GetVideoSourceConfigurationHandler(const std::map<std::string, std::string>& xs,
																		 const std::shared_ptr<pt::ptree>& configs,
																		 const std::shared_ptr<pt::ptree>& profiles_config)
			: OnvifRequestBase(GetVideoSourceConfiguration, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs),
				profiles_config_(profiles_config)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		pt::ptree request_xml;
		std::istringstream is(request->content.string());
		pt::xml_parser::read_xml(is, request_xml);

		// TODO: add way to search for child with full path like: "Envelope.Body.GetPr..."
		std::string requested_token;
		{
			auto envelope_node = exns::find("Envelope", request_xml);
			auto body_node = exns::find("Body", envelope_node->second);
			auto videosource_node = exns::find("GetVideoSourceConfiguration", body_node->second);
			auto profile_token = exns::find("ConfigurationToken", videosource_node->second);
			requested_token = profile_token->second.get_value<std::string>();
		}

		auto vs_config_list = profiles_config_->get_child("VideoSourceConfigurations");

		auto vs_config =
				std::find_if(vs_config_list.begin(), vs_config_list.end(), [requested_token](pt::ptree::value_type vs_obj) {
					return vs_obj.second.get<std::string>("token") == requested_token;
				});

		if (vs_config == vs_config_list.end())
			throw std::runtime_error("The requested configuration indicated with ConfigurationToken does not exist.");

		pt::ptree videosource_configuration_node;
		media::util::fill_soap_videosource_configuration(vs_config->second, videosource_configuration_node);

		auto envelope_tree = utility::soap::getEnvelopeTree(ns_);
		envelope_tree.add_child("s:Body.trt:GetVideoSourceConfigurationResponse.trt:Configuration",
														videosource_configuration_node);

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
			: OnvifRequestBase(GetVideoSourceConfigurations, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs),
				profiles_config_(profiles_config)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
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
	GetVideoSourcesHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs,
												 const utility::media::MediaProfilesManager& manager)
			: OnvifRequestBase(GetVideoSources, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs), manager_(manager)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		auto envelope_tree = utility::soap::getEnvelopeTree(ns_);

		pt::ptree response_node;
		auto append = [&](const pt::ptree& configured, const std::string& fps, const std::string& width,
											const std::string& height) {
			pt::ptree node;
			node.put("<xmlattr>.token", configured.get<std::string>("token"));
			node.put("tt:Framerate", configured.get<std::string>(fps));
			node.put("tt:Resolution.tt:Width", configured.get<std::string>(width));
			node.put("tt:Resolution.tt:Height", configured.get<std::string>(height));
			response_node.add_child("trt:VideoSources", node);
		};
		if (const auto* topology = manager_.Topology())
		{
			for (const auto& [_, channel] : topology->Channels())
				append(channel.get_child("videoSource"), "frameRate", "width", "height");
		}
		else
		{
			for (const auto& [_, source] : service_configs_->get_child(GetVideoSources))
				append(source, "Framerate", "Resolution.Width", "Resolution.Height");
		}

		envelope_tree.add_child("s:Body.trt:GetVideoSourcesResponse", response_node);

		pt::ptree root_tree;
		root_tree.put_child("s:Envelope", envelope_tree);

		std::ostringstream os;
		pt::write_xml(os, root_tree);

		utility::http::fillResponseWithHeaders(*response, os.str());
	}

private:
	const utility::media::MediaProfilesManager& manager_;
};

struct GetSnapshotUriHandler : public OnvifRequestBase
{
private:
	const osrv::ServerConfigs& server_cfg_;
	const utility::media::MediaProfilesManager& profiles_mgr_;
	// const std::shared_ptr<pt::ptree>& profiles_cfg_;

public:
	GetSnapshotUriHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs,
												const utility::media::MediaProfilesManager& profiles_mgr, osrv::ServerConfigs& server_cfg)
			: OnvifRequestBase(GetSnapshotUri, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs), server_cfg_(server_cfg),
				profiles_mgr_(profiles_mgr)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		pt::ptree request_xml;
		std::istringstream is(request->content.string());
		pt::xml_parser::read_xml(is, request_xml);

		std::string profileToken = exns::find_hierarchy("Envelope.Body.GetSnapshotUri.ProfileToken", request_xml);

		const auto& profile_config = profiles_mgr_.GetProfileByToken(profileToken);
		const auto& srcCfg =
				profile_config.get<std::string>(CONFIGURATION_ENUMERATION[CONFIGURATION_TYPE::VIDEOSOURCE], "");
		const auto& encCfg =
				profile_config.get<std::string>(CONFIGURATION_ENUMERATION[CONFIGURATION_TYPE::VIDEOENCODER], "");

		if (srcCfg.empty() || encCfg.empty())
			throw incomplete_configuration();

		auto envelope_tree = utility::soap::getEnvelopeTree(ns_);
		envelope_tree.put(
				"s:Body.trt:GetSnapshotUriResponse.trt:MediaUri.tt:Uri",
				osrv::media2::util::generate_snapshot_url(server_cfg_)); // the file server is also running by the Media2 service
		envelope_tree.put("s:Body.trt:GetSnapshotUriResponse.trt:MediaUri.tt:InvalidAfterConnect", false);
		envelope_tree.put("s:Body.trt:GetSnapshotUriResponse.trt:MediaUri.tt:InvalidAfterReboot", false);
		envelope_tree.put("s:Body.trt:GetSnapshotUriResponse.trt:MediaUri.tt:Timeout", "PT0S");

		pt::ptree root_tree;
		root_tree.put_child("s:Envelope", envelope_tree);

		std::ostringstream os;
		pt::write_xml(os, root_tree);

		utility::http::fillResponseWithHeaders(*response, os.str());
	}
};

struct GetStreamUriHandler : public OnvifRequestBase
{
	GetStreamUriHandler(const std::map<std::string, std::string>& xs, const std::shared_ptr<pt::ptree>& configs,
											osrv::ServerConfigs& server_cfg, const std::shared_ptr<pt::ptree>& profiles_cfg)
			: OnvifRequestBase(GetStreamUri, auth::SECURITY_LEVELS::READ_MEDIA, xs, configs), server_cfg_(server_cfg),
				profiles_cfg_(profiles_cfg)
	{
	}

	void operator()(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) override
	{
		pt::ptree request_xml;
		std::istringstream is(request->content.string());
		pt::xml_parser::read_xml(is, request_xml);

		const auto& operation = StreamOperation(request_xml, "GetStreamUri");
		auto base = StreamAddress(server_cfg_, operation, false, HttpServer::IsSecure(request));

		std::string requested_token = exns::find_hierarchy("Envelope.Body.GetStreamUri.ProfileToken", request_xml);
		// logger_->Debug("Requested token to get URI: " + requested_token);

		auto profiles_config_list = profiles_cfg_->get_child("MediaProfiles");

		auto profile_config =
				std::ranges::find_if(profiles_config_list, [&requested_token](const pt::ptree::value_type& vs_obj) {
					return vs_obj.second.get<std::string>("token") == requested_token;
				});

		if (profile_config == profiles_config_list.end())
			throw osrv::no_such_profile();

		auto encoder_token = profile_config->second.get<std::string>(CONFIGURATION_ENUMERATION[CONFIGURATION_TYPE::VIDEOENCODER]);

		auto stream_configs_list = service_configs_->get_child("GetStreamUri");
		auto stream_config_it =
				std::ranges::find_if(stream_configs_list, [&encoder_token](const pt::ptree::value_type& el) {
					return el.second.get<std::string>("VideoEncoderToken") == encoder_token;
				});

		if (stream_config_it == stream_configs_list.end())
			throw std::runtime_error("Could not find a stream for the requested Media Profile.");

		pt::ptree response_node;

		auto rtsp_url = base + stream_config_it->second.get<std::string>("Uri");
		response_node.put("trt:MediaUri.tt:Uri", rtsp_url);
		response_node.put("trt:MediaUri.tt:InvalidAfterConnect",
											stream_config_it->second.get<std::string>("InvalidAfterConnect"));
		response_node.put("trt:MediaUri.tt:InvalidAfterReboot",
											stream_config_it->second.get<std::string>("InvalidAfterReboot"));
		response_node.put("trt:MediaUri.tt:Timeout", stream_config_it->second.get<std::string>("Timeout"));

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

} // namespace media

MediaService::MediaService(const std::string& service_uri, const std::string& service_name,
													 std::shared_ptr<IOnvifServer> srv)
		: IOnvifService(service_uri, service_name, srv)
{
	auto& manager = *srv->MediaProfilesManager();
	auto add = [&](const std::string& operation, auth::SECURITY_LEVELS level, const std::string& type,
								 const std::string& element) {
		requestHandlers_.push_back(std::make_shared<media::ChannelConfigurationHandler>(
				operation, level, xml_namespaces_, configs_ptree_, manager, type, element));
	};
	using Level = auth::SECURITY_LEVELS;
	for (const auto& [type, element] : {std::pair{"VideoSource", "VideoSource"}, {"VideoEncoder", "VideoEncoder"},
																			{"AudioSource", "AudioSource"}, {"AudioEncoder", "AudioEncoder"},
																			{"Analytics", "VideoAnalytics"}, {"PTZ", "PTZ"}})
	{
		add(std::string("Add") + element + "Configuration", Level::ACTUATE, type, element);
		add(std::string("Remove") + element + "Configuration", Level::ACTUATE, type, element);
		if (std::string_view(type) != "PTZ")
			add(std::string("GetCompatible") + element + "Configurations", Level::READ_MEDIA, type, element);
	}
	add("GetAudioSourceConfigurations", Level::READ_MEDIA, "AudioSource", "AudioSource");
	add("GetAudioEncoderConfigurations", Level::READ_MEDIA, "AudioEncoder", "AudioEncoder");
	add("GetVideoEncoderConfigurations", Level::READ_MEDIA, "VideoEncoder", "VideoEncoder");
	AddMetadataHandlers(requestHandlers_, xml_namespaces_, configs_ptree_, srv, false);
	requestHandlers_.push_back(
			std::make_shared<media::GetAudioDecoderConfigurationsHandler>(xml_namespaces_, configs_ptree_));
	requestHandlers_.push_back(std::make_shared<media::GetAudioEncoderConfigurationOptionsHandler>(
			xml_namespaces_, configs_ptree_, srv->ProfilesConfig()));
	requestHandlers_.push_back(std::make_shared<media::GetAudioEncoderConfigurationHandler>(
			xml_namespaces_, configs_ptree_, srv->ProfilesConfig()));
	requestHandlers_.push_back(std::make_shared<media::GetAudioOutputsHandler>(xml_namespaces_, configs_ptree_));
	requestHandlers_.push_back(
			std::make_shared<media::GetAudioSourcesHandler>(xml_namespaces_, configs_ptree_, srv->ProfilesConfig()));
	requestHandlers_.push_back(std::make_shared<media::GetProfileHandler>(xml_namespaces_, configs_ptree_,
																																				manager));
	requestHandlers_.push_back(std::make_shared<media::GetProfilesHandler>(xml_namespaces_, configs_ptree_,
																																				 manager));
	requestHandlers_.push_back(
			std::make_shared<media::GetVideoAnalyticsConfigurationsHandler>(xml_namespaces_, configs_ptree_, *srv->MediaProfilesManager()));
	requestHandlers_.push_back(std::make_shared<media::GetVideoSourceConfigurationHandler>(
			xml_namespaces_, configs_ptree_, srv->ProfilesConfig()));
	requestHandlers_.push_back(std::make_shared<media::GetVideoSourceConfigurationsHandler>(
			xml_namespaces_, configs_ptree_, srv->ProfilesConfig()));
	requestHandlers_.push_back(std::make_shared<media::GetVideoSourcesHandler>(
			xml_namespaces_, configs_ptree_, *srv->MediaProfilesManager()));
	requestHandlers_.push_back(std::make_shared<media::GetSnapshotUriHandler>(
			xml_namespaces_, configs_ptree_, *srv->MediaProfilesManager(), *srv->GetServerConfigs()));
	requestHandlers_.push_back(std::make_shared<media::GetStreamUriHandler>(
			xml_namespaces_, configs_ptree_, *srv->GetServerConfigs(), srv->ProfilesConfig()));
}
} // namespace osrv



void fill_soap_media_profile(const pt::ptree& profileConfigs, pt::ptree& profileOutNode,
														 const utility::media::MediaProfilesManager& media)
{
	const auto& profiles_cfg = media.ReaderWriter()->ConfigsTree();
	profileOutNode.put("<xmlattr>.token", profileConfigs.get<std::string>("token"));
	profileOutNode.put("<xmlattr>.fixed", profileConfigs.get<std::string>("fixed"));
	profileOutNode.put("tt:Name", profileConfigs.get<std::string>("Name"));

	pt::ptree emptyIfNoNode;
	// Videosource
	{
		const std::string vs_token = profileConfigs.get<std::string>("VideoSource", "");
		auto vs_configs_list = profiles_cfg.get_child("VideoSource", emptyIfNoNode);
		auto vs_config = std::ranges::find_if(vs_configs_list, [&vs_token](const pt::ptree::value_type& vs_obj) {
			return vs_obj.second.get<std::string>("token") == vs_token;
		});

		if (vs_config != vs_configs_list.end())
		{
			pt::ptree videosource_configuration;
			osrv::media::util::fill_soap_videosource_configuration(vs_config->second, videosource_configuration);
			profileOutNode.put_child("tt:VideoSourceConfiguration", videosource_configuration);
		}
	}

	// Audio source
	{
		const std::string as_token = profileConfigs.get<std::string>("AudioSource", "");

		if (!as_token.empty())
		{
			pt::ptree as_node;

			auto as_config = utility::AudioSourceConfigsReaderByToken(as_token, profiles_cfg).AudioSource();
			as_node.add("<xmlattr>.token", as_token);
			as_node.add("tt:Name", as_config.get<std::string>("Name"));
			as_node.add("tt:UseCount", as_config.get<int>("UseCount"));
			as_node.add("tt:SourceToken", as_config.get<std::string>("SourceToken"));

			profileOutNode.put_child("tt:AudioSourceConfiguration", as_node);
		}
	}

	// VideoEncoder
	{
		auto ve_configs_list = profiles_cfg.get_child("VideoEncoderConfigurations", emptyIfNoNode);
		std::string ve_token = profileConfigs.get<std::string>("VideoEncoder", "");
		auto ve_config = std::ranges::find_if(ve_configs_list, [&ve_token](const pt::ptree::value_type& ve_obj) {
			return ve_obj.second.get<std::string>("token") == ve_token;
		});

		if (ve_config != ve_configs_list.end())
		{
			profileOutNode.put("tt:VideoEncoderConfiguration.<xmlattr>.token", ve_config->second.get<std::string>("token"));
			profileOutNode.put("tt:VideoEncoderConfiguration.<xmlattr>.GuaranteedFrameRate",
												 ve_config->second.get<std::string>("GuaranteedFrameRate"));
			profileOutNode.put("tt:VideoEncoderConfiguration.tt:Name", ve_config->second.get<std::string>("Name"));
			profileOutNode.put("tt:VideoEncoderConfiguration.tt:UseCount", ve_config->second.get<std::string>("UseCount"));
			profileOutNode.put("tt:VideoEncoderConfiguration.tt:Encoding", ve_config->second.get<std::string>("Encoding"));
			profileOutNode.put("tt:VideoEncoderConfiguration.tt:Resolution.tt:Width",
												 ve_config->second.get<std::string>("Resolution.Width"));
			profileOutNode.put("tt:VideoEncoderConfiguration.tt:Resolution.tt:Height",
												 ve_config->second.get<std::string>("Resolution.Height"));
			profileOutNode.put("tt:VideoEncoderConfiguration.tt:Quality", ve_config->second.get<std::string>("Quality"));

			// ratecontrol is optional
			if (auto ratecontrol_config_it = ve_config->second.find("RateControl");
					ratecontrol_config_it != ve_config->second.not_found())
			{
				profileOutNode.put("tt:VideoEncoderConfiguration.tt:RateControl.tt:FrameRateLimit",
													 ratecontrol_config_it->second.get<std::string>("FrameRateLimit"));
				profileOutNode.put("tt:VideoEncoderConfiguration.tt:RateControl.tt:EncodingInterval",
													 ratecontrol_config_it->second.get<std::string>("EncodingInterval"));
				profileOutNode.put("tt:VideoEncoderConfiguration.tt:RateControl.tt:BitrateLimit",
													 ratecontrol_config_it->second.get<std::string>("BitrateLimit"));
			}

			const auto& codec = ve_config->second.get<std::string>("Encoding");
			if ("H264" == codec)
			{
				// codecs info is optional
				auto h264_config_it = ve_config->second.find("H264");
				if (h264_config_it != ve_config->second.not_found())
				{
					profileOutNode.put("tt:VideoEncoderConfiguration.tt:H264.tt:GovLength",
														 h264_config_it->second.get<std::string>("GovLength"));
					profileOutNode.put("tt:VideoEncoderConfiguration.tt:H264.tt:H264Profile",
														 h264_config_it->second.get<std::string>("H264Profile"));
				}
			}
			else if ("MPEG4" == codec)
			{
				// TODO
			}

			// Multicast
			profileOutNode.put("tt:VideoEncoderConfiguration.tt:Multicast.tt:Address.tt:Type",
												 ve_config->second.get<std::string>("Multicast.Address.Type"));
			profileOutNode.put("tt:VideoEncoderConfiguration.tt:Multicast.tt:Address.tt:IPv4Address",
												 ve_config->second.get<std::string>("Multicast.Address.IPv4Address"));
			profileOutNode.put("tt:VideoEncoderConfiguration.tt:Multicast.tt:Port",
												 ve_config->second.get<std::string>("Multicast.Port"));
			profileOutNode.put("tt:VideoEncoderConfiguration.tt:Multicast.tt:TTL",
												 ve_config->second.get<std::string>("Multicast.TTL"));
			profileOutNode.put("tt:VideoEncoderConfiguration.tt:Multicast.tt:AutoStart",
												 ve_config->second.get<std::string>("Multicast.AutoStart"));

			profileOutNode.put("tt:VideoEncoderConfiguration.tt:SessionTimeout",
												 ve_config->second.get<std::string>("SessionTimeout"));
		}
	}

	// audio encoder
	{
		auto ae_token = profileConfigs.get<std::string>("AudioEncoder", "");
		if (!ae_token.empty())
		{
			pt::ptree ae_node;
			auto ae_config = utility::AudioEncoderReaderByToken(ae_token, profiles_cfg).AudioEncoder();

			osrv::media::util::fillAEConfig(ae_config, ae_node);

			profileOutNode.put_child("tt:AudioEncoderConfiguration", ae_node);
		}
	}
	if (auto token = profileConfigs.get<std::string>("Analytics", ""); !token.empty())
		for (const auto& [_, configuration] : profiles_cfg.get_child("Analytics", emptyIfNoNode))
			if (configuration.get<std::string>("token") == token)
			{
				pt::ptree analytics;
				osrv::analytics::fillConfiguration(configuration, analytics, profiles_cfg);
				profileOutNode.add_child("tt:VideoAnalyticsConfiguration", analytics);
			}
	if (const auto token = profileConfigs.get<std::string>("PTZ", ""); !token.empty())
	{
		pt::ptree ptz;
		osrv::ptz::fillPtzConfig(media.GetConfigByToken(token, "PTZ"), ptz, media);
		profileOutNode.add_child("tt:PTZConfiguration", ptz);
	}
	if (auto token = profileConfigs.get<std::string>("Metadata", ""); !token.empty())
		for (const auto& [_, configuration] : profiles_cfg.get_child("Metadata", emptyIfNoNode))
			if (configuration.get<std::string>("token") == token)
				profileOutNode.add_child("tt:MetadataConfiguration",
																 osrv::metadata::ConfigurationXml(configuration, profiles_cfg));
}

std::string osrv::media::util::generate_rtsp_url(const osrv::ServerConfigs& server_configs,
																								 const std::string& profile_stream_url)
{
	return server_configs.RtspAddress() + profile_stream_url;
}

void osrv::media::util::fill_soap_videosource_configuration(const pt::ptree& config_node, pt::ptree& videosource_node)
{
	videosource_node.put("<xmlattr>.token", config_node.get<std::string>("token"));
	videosource_node.put("tt:Name", config_node.get<std::string>("Name"));
	videosource_node.put("tt:UseCount", config_node.get<std::string>("UseCount"));
	videosource_node.put("<xmlattr>.ViewMode", config_node.get<std::string>("ViewMode"));
	videosource_node.put("tt:SourceToken", config_node.get<std::string>("SourceToken"));
	videosource_node.put("tt:Bounds.<xmlattr>.x", config_node.get<std::string>("Bounds.x"));
	videosource_node.put("tt:Bounds.<xmlattr>.y", config_node.get<std::string>("Bounds.y"));
	videosource_node.put("tt:Bounds.<xmlattr>.width", config_node.get<std::string>("Bounds.width"));
	videosource_node.put("tt:Bounds.<xmlattr>.height", config_node.get<std::string>("Bounds.height"));
}

void osrv::media::util::fill_analytics_configuration(pt::ptree& result)
{
	result.add("<xmlattr>.token", "analytics_token0");
	result.add("tt:Name", "Analytics0");
	result.add("tt:UseCount", 2);
	result.add("tt:AnalyticsEngineConfiguration", "");
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
	if (encoding == "PCMU" || encoding == "PCMA")
		encoding = "G711";
	if (encoding == "MP4A-LATM")
		encoding = "AAC";
	out.add("tt:Encoding", encoding);

	out.add("tt:Bitrate", in.get<int>("Bitrate"));
	out.add("tt:SampleRate", in.get<int>("SampleRate"));

	// TODO: hardcoded
	pt::ptree multicast_node;
	multicast_node.add("tt:Address.tt:Type", "IPv4");
	multicast_node.add("tt:Address.tt:IPv4Address", "239.0.1.0");
	multicast_node.add("tt:Port", 48000);
	multicast_node.add("tt:TTL", 3);
	multicast_node.add("tt:AutoStart", false);
	out.add_child("tt:Multicast", multicast_node);

	out.add("tt:SessionTimeout", "PT10S");
}
