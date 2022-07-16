#pragma once

#include "IOnvifServer.h"
#include "HttpServerFwd.h"
#include "Logger.h"
#include "RtspServer.h"

#include "utility/HttpDigestHelper.h"

#include "onvif_services\discovery_service.h"
#include "onvif_services\physical_components\IDigitalInput.h"

#include <boost/asio/io_context.hpp>

#include <memory>

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

	using socket_t = boost::asio::basic_stream_socket<boost::asio::ip::tcp>;
	using HttpServer = SimpleWeb::Server<socket_t>;

	struct ServerConfigs
	{
		std::string ipv4_address_;
		std::string http_port_;
		std::string rtsp_port_;

		bool enabled_http_port_forwarding;
		unsigned short forwarded_http_port;
		bool enabled_rtsp_port_forwarding;
		unsigned short forwarded_rtsp_port;

		std::vector<osrv::auth::UserAccount> system_users_;
		AUTH_SCHEME auth_scheme_{};
		std::shared_ptr<utility::digest::IDigestSession> digest_session_;

		DigitalInputsList digital_inputs_;
		
		std::shared_ptr<boost::asio::io_context> io_context_;

		// milliseconds
		unsigned short network_delay_simulation_ = 0;

		bool multichannel_enabled_ = false;
		unsigned char channels_count_ = 0;

		std::string rtsp_streaming_file_;
	};

	class Server : public IOnvifServer
	{
	public:
		Server(const std::string& /*configs_dir*/, std::shared_ptr<ILogger> /*log*/);
		Server(const ILogger&) = delete;
		Server(ILogger&&) = delete;
		~Server();

		void init();

		//throws exceptions in error
		void run();

	private:
		//ILogger& logger_;

		//std::shared_ptr<osrv::HttpServer> http_server_instance_ = nullptr;

		//osrv::ServerConfigs server_configs_;

		rtsp::Server* rtspServer_ = nullptr;

		std::shared_ptr<boost::asio::io_context> io_context_;
		std::shared_ptr<boost::asio::io_context::work> io_context_work_;
		std::shared_ptr<std::thread> io_context_thread_;
	};
	
	std::shared_ptr<ServerConfigs> read_server_configs(const std::string& /*config_path*/);

	DigitalInputsList read_digital_inputs(const boost::property_tree::ptree& /*config_node*/);
}
