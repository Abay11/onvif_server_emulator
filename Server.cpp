#include <filesystem>
#include <set>
#include "Server.h"
#include "web/AdminUi.h"
#include "utility/TlsContext.h"
#include "../onvif_services/device_service.h"
#include "../onvif_services/discovery_service.h"
#include "../onvif_services/event_service.h"
#include "../onvif_services/pullpoint/pull_point.h"
#include "../onvif_services/pullpoint/event_generators.h"
#include "../onvif_services/imaging_service.h"
#include "../onvif_services/media2_service.h"
#include "../onvif_services/media_service.h"
#include "../onvif_services/physical_components/IDigitalInput.h"
#include "../onvif_services/ptz_service.h"
#include "../onvif_services/recording_search_service.h"
#include "include/onvif_services/service_configs.h"

#include "utility/AnalyticsRules.h"
#include "utility/AuthHelper.h"
#include "utility/FieldDetector.h"
#include "utility/LineDetector.h"
#include "utility/MediaProfilesManager.h"
#include "utility/XmlParser.h"

#include "MediaFormats.h"
#include "utility/MetadataConfiguration.h"
#include "utility/MediaProfilesManager.h"

#include "HttpServer.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <string>
#include <cstdlib>
#include <format>

static const std::string COMMON_CONFIGS_NAME = "common.config";

namespace osrv
{

AUTH_SCHEME str_to_auth(const std::string& /*scheme*/);

Server::Server(const std::string& configs_dir, std::shared_ptr<ILogger> log) : IOnvifServer(configs_dir, log)
{
}

Server::~Server()
{
	if (https_server_) https_server_->stop();
	if (http_server_) http_server_->stop();
	discovery::stop();
	event::stop_service();

	io_context_work_.reset();
	try
	{
		if (io_context_thread_ && io_context_thread_->joinable())
		{
			io_context_thread_->join();
			logger_->Debug("Async IO Context's thread is joined.");
		}
	}
	catch (const std::exception&)
	{
	}

	if (rtspServer_)
		delete rtspServer_;
}

void Server::init(std::function<void()> reboot)
{
	http_server_->default_resource["GET"] = [](std::shared_ptr<HttpServer::Response> response,
																						 std::shared_ptr<HttpServer::Request> request) {
		response->write(SimpleWeb::StatusCode::client_error_bad_request, "Could not open path " + request->path);
	};

	http_server_->default_resource["POST"] = [this](std::shared_ptr<HttpServer::Response> response,
																									std::shared_ptr<HttpServer::Request> request) {
		logger_->Warn("The server could not handle a request:" + request->method + " " + request->path);
		response->write(SimpleWeb::StatusCode::client_error_bad_request, "Bad request");
	};

	http_server_->on_error = [this](std::shared_ptr<HttpServer::Request> request, const SimpleWeb::error_code& ec) {
		// if (ec != SimpleWeb::errc::operation_canceled && SimpleWeb::error_code::)
		//{
		//	logger_->Error("HTTP server internal error: " + ec.message());
		//}
	};

	auto configs_dir = configs_path_ + "/";
	server_configs_ = read_server_configs(configs_dir + COMMON_CONFIGS_NAME);
	if (server_configs_->https_enabled_)
	{
		https_server_ = std::make_shared<HttpServer>();
		https_server_->tls_context = utility::MakeTlsContext(server_configs_->tls_certificate_,
				server_configs_->tls_private_key_, server_configs_->advertised_ipv4_);
		https_server_->config.address = server_configs_->ipv4_address_;
		https_server_->config.port = std::stoi(server_configs_->https_port_);
	}
	server_configs_->journal->Add("info", "system", "application", "Application initialization started");

	http_server_->config.address = server_configs_->ipv4_address_;
	http_server_->config.port = std::stoi(server_configs_->http_port_);

	if (server_configs_->enabled_http_port_forwarding)
		logger_->Info("HTTP port forwarding simulated on port: " + std::to_string(server_configs_->forwarded_http_port));

	if (server_configs_->enabled_rtsp_port_forwarding)
		logger_->Info("RTSP port forwarding simulated on port: " + std::to_string(server_configs_->forwarded_rtsp_port));

	server_configs_->digest_session_ = std::make_shared<utility::digest::DigestSessionImpl>();
	// TODO: here is the same list is copied into digest_session, although it's already stored in server_configs
	server_configs_->digest_session_->set_users_list(server_configs_->system_users_);

	// Both SOAP services share the manager's authoritative tree. Streaming receives copies.
	MediaProfilesManager()->ActivateTopology(configs_dir + "channels.config");
	profiles_config_ = MediaProfilesManager()->SharedConfigs();
	auto archive = std::make_shared<RecordingArchive>(configs_dir + "recording_archive.json");
	server_configs_->archive = archive;
	auto available = [this](const auto& channel, const auto& profile) { return RecordingProfileAvailable(channel, profile); };
	archive->Resume(available);
	if (reboot)
		server_configs_->reboot = [archive, reboot = std::move(reboot), available] {
			archive->Suspend();
			try
			{
				reboot();
			}
			catch (...)
			{
				archive->Resume(available);
				throw;
			}
		};
	MediaProfilesManager()->MetadataAvailable(server_configs_->rtsp_metadata_enabled_);
	if (!server_configs_->rtsp_metadata_enabled_)
		for (const auto& [_, profile] : profiles_config_->get_child("MediaProfiles"))
			if (!profile.get<std::string>("Metadata", "").empty())
				throw std::runtime_error("Profiles reference metadata but rtsp_metadata.enabled is false; enable the transport "
																 "or remove the metadata associations");

	// Validate and join discovery before event generators create background threads.
	// A wrong mirrored interface must fail startup cleanly.
	discovery::init_service(configs_dir, *logger_, *server_configs_);
	discovery::start();

	AnalyticsService()->Run();
	DeviceService()->Run();
	DeviceIOService()->Run();
	ImagingService()->Run();
	MediaService()->Run();
	Media2Service()->Run();
	PTZService()->Run();
	RecordingControlService()->Run();
	RecordingSearchService()->Run();
	ReplayControlService()->Run();

	const std::vector<event::EventGeneratorRegistration> event_generators{
			{[detector = Rules()](const boost::property_tree::ptree&, boost::asio::io_context& io, const ILogger& logger) {
				 return std::make_shared<event::FaceRecognitionEventGenerator>(detector, io, logger);
			 },
			 {utility::face::Topic,
				{{"VideoSource", "tt:ReferenceToken"}, {"Rule", "xsd:string"}},
				{{"Likelihood", "xsd:float"}, {"Label", "xsd:string"}, {"EnrollmentID", "xsd:string"},
				 {"ImageUri", "xsd:anyURI"}, {"RefImageUri", "xsd:anyURI"}},
				false,
				{{"ObjectId", "tt:ObjectId"}, {"ParentObjectId", "tt:ObjectId"},
				 {"BoundingBox", "tt:Rectangle"}, {"Image", "xsd:base64Binary"}}}},
			{[detector = Rules()](const boost::property_tree::ptree&, boost::asio::io_context& io, const ILogger& logger) {
				 return std::make_shared<event::LicensePlateEventGenerator>(detector, io, logger);
			 },
			 {utility::lpr::Topic,
				{{"VideoSource", "tt:ReferenceToken"}, {"Rule", "xsd:string"}},
				{{"Likelihood", "xsd:float"},
				 {"Speed", "xsd:float"},
				 {"Direction", "tt:Direction"},
				 {"ImageURI", "xsd:anyURI"},
				 {"VehicleImageURI", "xsd:anyURI"}},
				false,
				{{"LicensePlateInfo", "tt:LicensePlateInfo"},
				 {"BoundingBox", "tt:Rectangle"},
				 {"VehicleInfo", "tt:VehicleInfo"},
				 {"Image", "xsd:base64Binary"},
				 {"VehicleImage", "xsd:base64Binary"}}}},
			{[detector = Rules()](const boost::property_tree::ptree& configs, boost::asio::io_context& io,
														const ILogger& logger) {
				 return std::make_shared<event::MotionRegionEventGenerator>(
						 detector, configs.get<int>("MotionRegion.EventGenerationTimeout", 30), io, logger);
			 },
			 {utility::AnalyticsRules::Topic,
				{{"VideoSource", "tt:ReferenceToken"}, {"RuleName", "xsd:string"}},
				{{"State", "xsd:boolean"}}}},
			{[detector = Rules()](const boost::property_tree::ptree& configs, boost::asio::io_context& io,
														const ILogger& logger) {
				 return std::make_shared<event::LineDetectorEventGenerator>(
						 detector, configs.get<int>("LineDetector.EventGenerationTimeout", 30), io, logger);
			 },
			 {utility::line::Topic,
				{{"VideoSource", "tt:ReferenceToken"}, {"AnalyticsConfiguration", "tt:ReferenceToken"}, {"Rule", "xsd:string"}},
				{{"ObjectId", "tt:StringList"}},
				false}},
			{[detector = Rules()](const boost::property_tree::ptree&, boost::asio::io_context& io, const ILogger& logger) {
				 return std::make_shared<event::LineCountingEventGenerator>(detector, io, logger);
			 },
			 {utility::counting::Topic,
				{{"VideoSource", "tt:ReferenceToken"}, {"AnalyticsConfiguration", "tt:ReferenceToken"}, {"Rule", "xsd:string"}},
				{{"Count", "xsd:int"}}}},
			{[detector = Rules()](const boost::property_tree::ptree&, boost::asio::io_context& io, const ILogger& logger) {
				 return std::make_shared<event::FieldDetectorEventGenerator>(detector, io, logger);
			 },
			 {utility::field::Topic,
				{{"VideoSource", "tt:ReferenceToken"}, {"AnalyticsConfiguration", "tt:ReferenceToken"}, {"Rule", "xsd:string"}},
				{{"IsInside", "xsd:boolean"}}}}};
	event_source_ = std::make_shared<event::EventSource>(*logger_, server_configs_->journal, server_configs_->detectors);
	server_configs_->event_source = event_source_;
	event::init_service(*http_server_, *server_configs_, configs_dir, *logger_, event_source_, event_generators, *MediaProfilesManager()->Topology());

	std::optional<rtsp::AudioInfo> audio;
	const auto& encoders = profiles_config_->get_child("AudioEncoder");
	if (!encoders.empty())
	{
		const auto& encoder = encoders.front().second;
		audio.emplace(encoder.get<std::string>("Encoding"), encoder.get<unsigned>("Bitrate") * 1000,
									encoder.get<unsigned>("SampleRate") * 1000);
	}
	rtspServer_ = new rtsp::Server(&*logger_, *server_configs_, audio, event_source_, Rules()->Scenes());
	server_configs_->prepare_replay = [this](const auto& token) { rtspServer_->ConfigureRecording(token); };
	// Keep profile URIs valid after disabling metadata, without requiring its payloader.
	Metadata()->Bind({},
			[this](const auto& path) { rtspServer_->SynchronizeProfile(path); },
			[this](const auto& paths) { rtspServer_->RetainProfiles(paths); },
			[this](const auto& path, const auto* video, const auto* audio, auto settings) {
				rtspServer_->ConfigureChannelProfile(path, video, audio, std::move(settings));
			});
	Metadata()->Refresh();

	if (auto delay = server_configs_->network_delay_simulation_; delay > 0)
	{
		logger_->Info("Network delay simulation is enabled. Equals (ms): " + std::to_string(delay));
	}

	io_context_ = std::make_shared<boost::asio::io_context>();
	io_context_work_ = std::make_shared<boost::asio::io_context::work>(*io_context_);
	io_context_thread_ = std::make_shared<std::thread>([this]() {
		logger_->Debug("Async IO Context's thread is running...");
		io_context_->run();
	});

	server_configs_->io_context_ = io_context_;
	auto tunnel_address = server_configs_->ipv4_address_ == "0.0.0.0" ? "127.0.0.1" : server_configs_->ipv4_address_;
	http_server_->rtsp_tunnel_endpoint = {boost::asio::ip::make_address(tunnel_address),
			static_cast<unsigned short>(std::stoi(server_configs_->rtsp_port_))};
	RegisterAdminUi(*this);
	if (https_server_)
	{
		https_server_->rtsp_tunnel_endpoint = http_server_->rtsp_tunnel_endpoint;
		https_server_->resource = http_server_->resource;
		https_server_->default_resource = http_server_->default_resource;
		https_server_->on_error = http_server_->on_error;
	}
}

void Server::run()
{
	// Bind synchronously before dispatching requests so partial startup cannot leave a listener running.
	auto io = std::make_shared<boost::asio::io_context>();
	try
	{
		if (server_configs_->http_enabled_)
		{
			http_server_->io_service = io;
			http_server_->start();
		}
		if (https_server_)
		{
			https_server_->io_service = io;
			https_server_->start();
		}
		rtspServer_->run();
		logger_->Info("HTTP/HTTPS and RTSP listeners ready");
		server_configs_->journal->Add("info", "system", "application", "Application started; web and RTSP listeners ready");
		boost::asio::signal_set shutdown(*io, SIGINT, SIGTERM);
		shutdown.async_wait([io](const boost::system::error_code& error, int) {
			if (!error)
				io->stop();
		});
		io->run();
		server_configs_->archive->Suspend();
	}
	catch (...)
	{
		http_server_->stop();
		if (https_server_) https_server_->stop();
		io->stop();
		throw;
	}
}

std::shared_ptr<ServerConfigs> read_server_configs(const std::string& config_path)
{

	std::ifstream configs_file(config_path);
	if (!configs_file.is_open())
		throw std::runtime_error("Could not read a config file");

	namespace pt = boost::property_tree;
	pt::ptree configs_tree;
	pt::read_json(configs_file, configs_tree);

	auto read_configs = std::make_shared<ServerConfigs>();

	read_configs->ipv4_address_ = configs_tree.get<std::string>("addresses.ipv4");
	read_configs->http_port_ = configs_tree.get<std::string>("addresses.http_port");
	read_configs->https_port_ = configs_tree.get<std::string>("addresses.https_port", "8443");
	read_configs->http_enabled_ = configs_tree.get<bool>("http.enabled", true);
	read_configs->https_enabled_ = configs_tree.get<bool>("https.enabled", true);
	if (!read_configs->http_enabled_ && !read_configs->https_enabled_)
		throw std::runtime_error("At least one of HTTP and HTTPS must be enabled");
	auto port = [](const std::string& value) {
		if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos ||
				std::stoul(value) == 0 || std::stoul(value) > 65535)
			throw std::runtime_error("HTTP/HTTPS ports must be integers in 1..65535");
		return std::stoul(value);
	};
	const auto http_port = port(read_configs->http_port_);
	const auto https_port = port(read_configs->https_port_);
	if (read_configs->http_enabled_ && read_configs->https_enabled_ && http_port == https_port)
		throw std::runtime_error("HTTP and HTTPS must use different ports");
	auto path = [&](const char* key) {
		auto value = configs_tree.get<std::string>(key, "");
		return value.empty() ? value : (std::filesystem::path(config_path).parent_path() / value).string();
	};
	read_configs->tls_certificate_ = path("https.certificate");
	read_configs->tls_private_key_ = path("https.private_key");
	if (read_configs->https_enabled_ && read_configs->tls_certificate_.empty() != read_configs->tls_private_key_.empty())
		throw std::runtime_error("HTTPS certificate and private key must both be configured");
	read_configs->rtsp_port_ = configs_tree.get<std::string>("addresses.rtsp_port");
	read_configs->advertised_ipv4_ = configs_tree.get<std::string>("advertised.ipv4", "127.0.0.1");
	if (const char* value = std::getenv("ONVIF_ADVERTISED_IPV4"))
		read_configs->advertised_ipv4_ = value;
	const auto advertised = boost::asio::ip::make_address_v4(read_configs->advertised_ipv4_);
	if (advertised.is_unspecified() || advertised.is_multicast() || advertised.to_uint() == 0xffffffff)
		throw std::runtime_error("advertised.ipv4 must be a unicast IPv4 address reachable by clients");
	read_configs->advertised_web_scheme_ = configs_tree.get<std::string>("advertised.web_scheme", "auto");
	const auto& scheme = read_configs->advertised_web_scheme_;
	if ((scheme != "auto" && scheme != "http" && scheme != "https") ||
			(scheme == "http" && !read_configs->http_enabled_) ||
			(scheme == "https" && !read_configs->https_enabled_))
		throw std::runtime_error("advertised.web_scheme must select an enabled HTTP/HTTPS listener, or auto");
	read_configs->advertised_http_port_ = configs_tree.get<unsigned short>("advertised.http_port", 0);
	read_configs->advertised_https_port_ = configs_tree.get<unsigned short>("advertised.https_port", 0);
	read_configs->advertised_rtsp_port_ = configs_tree.get<unsigned short>("advertised.rtsp_port", 0);
	read_configs->advertised_pullpoint_port_ = configs_tree.get<unsigned short>("advertised.pullpoint_port", 0);
	read_configs->rtp_port_min_ = configs_tree.get<unsigned short>("rtp.port_min", 50000);
	read_configs->rtp_port_max_ = configs_tree.get<unsigned short>("rtp.port_max", 50199);
	read_configs->rtsp_metadata_enabled_ = configs_tree.get<bool>("rtsp_metadata.enabled", true);
	if (read_configs->rtp_port_min_ == 0 || read_configs->rtp_port_min_ % 2 != 0 ||
		read_configs->rtp_port_max_ <= read_configs->rtp_port_min_ || read_configs->rtp_port_max_ % 2 != 1)
		throw std::runtime_error("RTP range must start at a positive even port and end at a larger odd port");

	read_configs->enabled_http_port_forwarding =
			configs_tree.get<bool>("portForwardingSimulation.enabled_for_http", false);
	if (read_configs->enabled_http_port_forwarding)
		read_configs->forwarded_http_port = configs_tree.get<unsigned short>("portForwardingSimulation.http_port");

	read_configs->enabled_rtsp_port_forwarding =
			configs_tree.get<bool>("portForwardingSimulation.enabled_for_rtsp", false);
	if (read_configs->enabled_rtsp_port_forwarding)
		read_configs->forwarded_rtsp_port = configs_tree.get<unsigned short>("portForwardingSimulation.rtsp_port");

	auto auth_scheme = configs_tree.get<std::string>("authentication");
	read_configs->auth_scheme_ = str_to_auth(auth_scheme);

	pt::ptree users_tree;
	pt::read_json((std::filesystem::path(config_path).parent_path() / "users.config").string(), users_tree);
	auto users_node = users_tree.get_child("users");
	std::set<std::string> logins;
	if (users_node.empty())
		throw std::runtime_error("Could not read Users list");

	for (auto user : users_node)
	{
		const auto login = user.second.get<std::string>(auth::UserAccount::LOGIN);
		if (login.empty() || login.find(':') != std::string::npos ||
			std::any_of(login.begin(), login.end(), [](unsigned char c) { return c < 32 || c == 127; }) ||
			!logins.insert(login).second)
			throw std::runtime_error("User login must be nonempty, unique and contain no colon");
		read_configs->system_users_.emplace_back(osrv::auth::UserAccount{
				user.second.get<std::string>(auth::UserAccount::LOGIN), user.second.get<std::string>(auth::UserAccount::PASS),
				osrv::auth::str_to_usertype(user.second.get<std::string>(auth::UserAccount::TYPE)),
				user.second.get<bool>("enabled")});
	}

	read_configs->network_delay_simulation_ = configs_tree.get<unsigned short>("networkDelaySimulation.milliseconds");


	if (configs_tree.get<bool>("fileStreaming.enabled"))
	{
		read_configs->rtsp_streaming_file_ = configs_tree.get<std::string>("fileStreaming.filePath");
	}

	return read_configs;
}

DigitalInputsList read_digital_inputs(const boost::property_tree::ptree& configs_node)
{
	std::vector<std::shared_ptr<IDigitalInput>> result;
	for (const auto& t : configs_node)
	{
		auto di = std::make_shared<SimpleDigitalInputImpl>(
				SimpleDigitalInputImpl(t.second.get<std::string>("Token"), t.second.get<bool>("InitialState")));

		if (t.second.get<bool>("GenerateEvent"))
		{
			di->Enable();
		}
		else
		{
			di->Disable();
		}

		result.push_back(di);
	}

	return result;
}

bool ServerConfigs::AdvertiseHttps() const
{
	return advertised_web_scheme_ == "https" || (advertised_web_scheme_ == "auto" && !http_enabled_);
}

std::string ServerConfigs::HttpAddress() const
{
	return AdvertiseHttps() ? HttpsAddress() : PlainHttpAddress();
}

std::string ServerConfigs::PlainHttpAddress() const
{
	const auto port = advertised_http_port_ ? std::to_string(advertised_http_port_) :
		(enabled_http_port_forwarding ? std::to_string(forwarded_http_port) : http_port_);
	return std::format("http://{}:{}/", advertised_ipv4_, port);
}

std::string ServerConfigs::HttpsAddress() const
{
	return std::format("https://{}:{}/", advertised_ipv4_,
			advertised_https_port_ ? std::to_string(advertised_https_port_) : https_port_);
}

std::string ServerConfigs::RtspAddress() const
{
	const auto port = advertised_rtsp_port_ ? std::to_string(advertised_rtsp_port_) :
		(enabled_rtsp_port_forwarding ? std::to_string(forwarded_rtsp_port) : rtsp_port_);
	return std::format("rtsp://{}:{}/", advertised_ipv4_, port);
}

std::string ServerConfigs::PullPointAddress(unsigned short listen_port, bool use_http_port) const
{
	if (use_http_port)
		return HttpAddress();
	return std::format("{}://{}:{}/", AdvertiseHttps() ? "https" : "http", advertised_ipv4_,
		advertised_pullpoint_port_ ? advertised_pullpoint_port_ : listen_port);
}

AUTH_SCHEME str_to_auth(const std::string& scheme)
{
	if (scheme == "digest/ws-security")
		return AUTH_SCHEME::DIGEST_WSS;

	if (scheme == "digest")
		return AUTH_SCHEME::DIGEST;

	if (scheme == "ws-security")
		return AUTH_SCHEME::WSS;

	return AUTH_SCHEME::NONE;
}

} // namespace osrv
