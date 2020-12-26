#pragma once

#include "Types.inl"
#include "Logger.h"
#include "RtspServer.h"
#include "utility/HttpDigestHelper.h"
#include "onvif_services\discovery_service.h"
#include "onvif_services\physical_components\IDigitalInput.h"

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
		std::string ipv4_address_;
		std::string http_port_;
		std::string rtsp_port_;

		bool enabled_http_port_forwarding;
		unsigned short forwarded_http_port;
		bool enabled_rtsp_port_forwarding;
		unsigned short forwarded_rtsp_port;

		UsersList_t system_users_;
		AUTH_SCHEME auth_scheme_{};
		DigestSessionSP digest_session_;

		DigitalInputsList digital_inputs_;
	};

	class Server
	{
	public:
		Server(std::string /*configs_dir*/, ILogger& /*log*/);
		Server(const ILogger&) = delete;
		Server(ILogger&&) = delete;
		~Server();

		//throws exceptions in error
		void run();

	private:
		ILogger& logger_;

		std::shared_ptr<HttpServer> http_server_instance_ = nullptr;

		ServerConfigs server_configs_;

		rtsp::Server* rtspServer_;
	};
	
	ServerConfigs read_server_configs(const std::string& /*config_path*/);

	DigitalInputsList read_digital_inputs(const boost::property_tree::ptree& /*config_node*/);
}
