#include "Server.h"
#include "../onvif_services/device_service.h"
#include "../onvif_services/media_service.h"
#include "../onvif_services/media2_service.h"
#include "../onvif_services/event_service.h"

#include "utility/AuthHelper.h"

#include "Simple-Web-Server\server_http.hpp"

#include <string>

#include <boost\property_tree\json_parser.hpp>

static const std::string COMMON_CONFIGS_NAME = "common.config";

static char RTSP_PORT[] = "8554";

namespace osrv
{
	
	AUTH_SCHEME str_to_auth(const std::string& /*scheme*/);

	Server::Server(std::string configs_dir, Logger& log)
		: log_(log)
		,http_server_instance_(new HttpServer)
		,rtspServer_(new rtsp::Server(&log, RTSP_PORT))
	{
		http_server_instance_->config.port = MASTER_PORT;
	
		http_server_instance_->default_resource["GET"] = [](std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request)
		{
			response->write(SimpleWeb::StatusCode::client_error_bad_request, "Could not open path " + request->path);
		};
		
		http_server_instance_->default_resource["POST"] = [this](std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request)
		{
			log_.Warn("The server could not handle a request:" + request->method + " " + request->path);
			response->write(SimpleWeb::StatusCode::client_error_bad_request, "Bad request");
		};

		configs_dir += "/";
		server_configs_ = read_server_configs(configs_dir + COMMON_CONFIGS_NAME);
		server_configs_.digest_session_ = std::make_shared<utility::digest::DigestSessionImpl>();
		//TODO: here is the same list is copied into digest_session, although it's already stored in server_configs
		server_configs_.digest_session_->set_users_list(server_configs_.system_users_);

		device::init_service(*http_server_instance_, server_configs_, configs_dir, log);
		media::init_service(*http_server_instance_, configs_dir, log);
		media2::init_service(*http_server_instance_, configs_dir, log);
		event::init_service(*http_server_instance_, server_configs_, configs_dir, log);
	}

	Server::~Server()
	{
		delete rtspServer_;
	}

void Server::run()
{
	using namespace std;

	// Start server and receive assigned port when server is listening for requests
	promise<unsigned short> server_port;
	thread server_thread([this, &server_port]() {
		// Start server
		http_server_instance_->start([&server_port](unsigned short port) {
			server_port.set_value(port);
			});
		});

	rtspServer_->run();

	std::string msg("Server is successfully started on port: ");
	msg += std::to_string(server_port.get_future().get());
	log_.Info(msg);

	server_thread.join();
}

ServerConfigs read_server_configs(const std::string& config_path)
{
	std::ifstream configs_file(config_path);
	if (!configs_file.is_open())
		throw std::runtime_error("Could not read a config file");

	namespace pt = boost::property_tree;
	pt::ptree configs_tree;
	pt::read_json(configs_file, configs_tree);

	auto auth_scheme = configs_tree.get<std::string>("authentication");

	ServerConfigs read_configs;
	read_configs.auth_scheme_ = str_to_auth(auth_scheme);

	auto users_node = configs_tree.get_child("users");
	if (users_node.empty())
		throw std::runtime_error("Could not read Users list");

	for (auto user : users_node)
	{
		read_configs.system_users_.emplace_back(
			osrv::auth::UserAccount{
				user.second.get<std::string>(auth::UserAccount::LOGIN),
				user.second.get<std::string>(auth::UserAccount::PASS),
				osrv::auth::str_to_usertype(user.second.get<std::string>(auth::UserAccount::TYPE))
			});
	}

	return read_configs;
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
