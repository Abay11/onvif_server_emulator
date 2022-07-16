#include "Server.h"
#include "../onvif_services/device_service.h"
#include "../onvif_services/media_service.h"
#include "../onvif_services/media2_service.h"
#include "../onvif_services/event_service.h"
#include "../onvif_services/discovery_service.h"
#include "../onvif_services/imaging_service.h"
#include "../onvif_services/ptz_service.h"
#include "../onvif_services/recording_search_service.h"
#include "include/onvif_services/service_configs.h"

#include "utility/XmlParser.h"
#include "utility/AuthHelper.h"
#include "../onvif_services/physical_components/IDigitalInput.h"
#include "MediaFormats.h"

#include "Simple-Web-Server/server_http.hpp"

#include <boost/property_tree/json_parser.hpp>

#include <boost/asio/io_context.hpp>

#include <string>

static const std::string COMMON_CONFIGS_NAME = "common.config";

namespace osrv
{
	
	AUTH_SCHEME str_to_auth(const std::string& /*scheme*/);

	Server::Server(const std::string& configs_dir, std::shared_ptr<ILogger> log)
		: IOnvifServer(configs_dir, log)
	{
	}

	Server::~Server()
	{
		discovery::stop();

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

	void Server::init()
	{
		http_server_->default_resource["GET"] = [](std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request)
		{
			response->write(SimpleWeb::StatusCode::client_error_bad_request, "Could not open path " + request->path);
		};
		
		http_server_->default_resource["POST"] = [this](std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request)
		{
			logger_->Warn("The server could not handle a request:" + request->method + " " + request->path);
			response->write(SimpleWeb::StatusCode::client_error_bad_request, "Bad request");
		};

		http_server_->on_error = [this](std::shared_ptr<HttpServer::Request> request, const SimpleWeb::error_code& ec)
		{
			//if (ec != SimpleWeb::errc::operation_canceled && SimpleWeb::error_code::)
			//{
			//	logger_->Error("HTTP server internal error: " + ec.message());
			//}
		};

		auto configs_dir = configs_path_ + "/";
		server_configs_ = read_server_configs(configs_dir + COMMON_CONFIGS_NAME);

		http_server_->config.address = server_configs_->ipv4_address_;
		http_server_->config.port = std::stoi(server_configs_->http_port_);

		if (server_configs_->enabled_http_port_forwarding)
			logger_->Info("HTTP port forwarding simulated on port: " + std::to_string(server_configs_->forwarded_http_port));

		if (server_configs_->enabled_rtsp_port_forwarding)
			logger_->Info("RTSP port forwarding simulated on port: " + std::to_string(server_configs_->forwarded_rtsp_port));

		server_configs_->digest_session_ = std::make_shared<utility::digest::DigestSessionImpl>();
		//TODO: here is the same list is copied into digest_session, although it's already stored in server_configs
		server_configs_->digest_session_->set_users_list(server_configs_->system_users_);

		profiles_config_ = osrv::ServiceConfigs("media_profiles", configs_dir);

		DeviceService()->Run();
		ImagingService()->Run();
		MediaService()->Run();
		Media2Service()->Run();
		PTZService()->Run();
		RecordingSearchService()->Run();
		ReplayControlService()->Run();

		event::init_service(*http_server_, *server_configs_, configs_dir, *logger_);
		discovery::init_service(configs_dir, *logger_);

		// TODO: impl. logic for multichannel cannel
		auto audio_node = profiles_config_->get_child("AudioEncoderConfigurations").front();
		audio_node.second.get_value<std::string>("Encoding");
		audio_node.second.get_value<std::string>("Encoding");

		rtsp::AudioInfo ainfo{audio_node.second.get<std::string>("Encoding"),
			audio_node.second.get<unsigned int>("Bitrate") * 1000,
			audio_node.second.get<unsigned int>("SampleRate") * 1000,
		};

		rtspServer_ = new rtsp::Server(&*logger_, *server_configs_, std::move(ainfo));

		if (auto delay = server_configs_->network_delay_simulation_; delay > 0)
		{
			logger_->Info("Network delay simulation is enabled. Equals (ms): " + std::to_string(delay));
		}

		io_context_ = std::make_shared<boost::asio::io_context>();
		io_context_work_ = std::make_shared<boost::asio::io_context::work>(*io_context_);
		io_context_thread_ = std::make_shared<std::thread>(
			[this]()
			{
				logger_->Debug("Async IO Context's thread is running...");
				io_context_->run();
			}
		);

		server_configs_->io_context_ = io_context_;
	}

void Server::run()
{
	using namespace std;

	// Start server and receive assigned port when server is listening for requests
	promise<unsigned short> server_port;
	thread server_thread([this, &server_port]() {
			// Start HTTP server
			try
			{
				http_server_->start([&server_port](unsigned short port) {
					server_port.set_value(port);
					});
			}
			catch (const std::exception& e)
			{
				logger_->Error(std::string("HTTP server finished with a critical erroe: ") + e.what());
			}
		});

	rtspServer_->run();
	try
	{
		discovery::start();
	}
	catch (const std::exception& e)
	{
		std::string what(e.what());
		logger_->Error("Can't start Discovery Service: " + what);
	}

	std::string msg("Server is successfully started on port: ");
	msg += std::to_string(server_port.get_future().get());
	logger_->Info(msg);

	server_thread.join();
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
	read_configs->rtsp_port_ = configs_tree.get<std::string>("addresses.rtsp_port");

	read_configs->enabled_http_port_forwarding = configs_tree.get<bool>("portForwardingSimulation.enabled_for_http", false);
	if (read_configs->enabled_http_port_forwarding)
		read_configs->forwarded_http_port = configs_tree.get<unsigned short>("portForwardingSimulation.http_port");
	
	read_configs->enabled_rtsp_port_forwarding = configs_tree.get<bool>("portForwardingSimulation.enabled_for_rtsp", false);
	if (read_configs->enabled_rtsp_port_forwarding)
		read_configs->forwarded_rtsp_port = configs_tree.get<unsigned short>("portForwardingSimulation.rtsp_port");

	auto auth_scheme = configs_tree.get<std::string>("authentication");
	read_configs->auth_scheme_ = str_to_auth(auth_scheme);

	auto users_node = configs_tree.get_child("users");
	if (users_node.empty())
		throw std::runtime_error("Could not read Users list");

	for (auto user : users_node)
	{
		read_configs->system_users_.emplace_back(
			osrv::auth::UserAccount{
				user.second.get<std::string>(auth::UserAccount::LOGIN),
				user.second.get<std::string>(auth::UserAccount::PASS),
				osrv::auth::str_to_usertype(user.second.get<std::string>(auth::UserAccount::TYPE))
			});
	}

	read_configs->network_delay_simulation_ = configs_tree.get<unsigned short>("networkDelaySimulation.milliseconds");
	
	read_configs->multichannel_enabled_ = configs_tree.get<bool>("multichannelSimulation.enabled");
	read_configs->channels_count_ = configs_tree.get<unsigned char>("multichannelSimulation.channelCount");

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
			SimpleDigitalInputImpl(t.second.get<std::string>("Token"),
				t.second.get<bool>("InitialState")));

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

}
