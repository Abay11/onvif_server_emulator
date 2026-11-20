#pragma once

#include "HttpServerFwd.h"
#include "IOnvifServer.h"
#include "Logger.h"
#include "RtspServer.h"

#include "utility/HttpDigestHelper.h"

#include "onvif_services/discovery_service.h"
#include "onvif_services/physical_components/IDigitalInput.h"

#include <boost/asio/io_context.hpp>

#include <memory>

// namespace onvif server
namespace osrv
{
namespace event { class EventSource; }
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

	bool enabled_http_port_forwarding = false;
	unsigned short forwarded_http_port = 0;
	bool enabled_rtsp_port_forwarding = false;
	unsigned short forwarded_rtsp_port = 0;

	// Client-visible endpoints are independent of socket bind addresses.
	std::string advertised_ipv4_ = "127.0.0.1";
	unsigned short advertised_http_port_ = 0;
	unsigned short advertised_rtsp_port_ = 0;
	unsigned short advertised_pullpoint_port_ = 0;
	unsigned short rtp_port_min_ = 50000;
	unsigned short rtp_port_max_ = 50199;

	std::string HttpAddress() const;
	std::string RtspAddress() const;
	std::string PullPointAddress(unsigned short listen_port, bool use_http_port) const;

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
	bool rtsp_metadata_enabled_ = true;
};

class Server : public IOnvifServer
{
public:
	Server(const std::string& /*configs_dir*/, std::shared_ptr<ILogger> /*log*/);
	Server(const ILogger&) = delete;
	Server(ILogger&&) = delete;
	~Server();

	void init();

	// throws exceptions in error
	void run();

private:
	// ILogger& logger_;

	// std::shared_ptr<osrv::HttpServer> http_server_instance_ = nullptr;

	// osrv::ServerConfigs server_configs_;

	rtsp::Server* rtspServer_ = nullptr;
	std::shared_ptr<event::EventSource> event_source_;

	std::shared_ptr<boost::asio::io_context> io_context_;
	std::shared_ptr<boost::asio::io_context::work> io_context_work_;
	std::shared_ptr<std::thread> io_context_thread_;
};

std::shared_ptr<ServerConfigs> read_server_configs(const std::string& /*config_path*/);

DigitalInputsList read_digital_inputs(const boost::property_tree::ptree& /*config_node*/);
} // namespace osrv
