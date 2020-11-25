#pragma once

#include "Types.inl"
#include "Logger.hpp"
#include "RtspServer.h"
#include "utility/HttpDigestHelper.h"
#include "onvif_services\discovery_service.h"

#include <memory>

namespace {
	const std::string MASTER_ADDR = "127.0.0.1";
	const unsigned short MASTER_PORT = 8080;
}

//namespace onvif server
namespace osrv
{
	enum class AUTH_SCHEME
	{
		NONE = 0,
		WSS,
		DIGEST,
		DIGEST_WSS,
	};

	struct ServerConfigs
	{
		UsersList_t system_users_;
		AUTH_SCHEME auth_scheme_{};
		DigestSessionSP digest_session_;
	};

	class Server
	{
	public:
		Server(std::string /*configs_dir*/, Logger& /*log*/);
		Server(const Logger&) = delete;
		Server(Logger&&) = delete;
		~Server();

		//throws exceptions in error
		void run();

	private:
		Logger& log_;

		std::shared_ptr<HttpServer> http_server_instance_ = nullptr;

		ServerConfigs server_configs_;

		rtsp::Server* rtspServer_;
	};
	
	ServerConfigs read_server_configs(const std::string& /*config_path*/);
}
