#include "../RtspServer.h"
#include "../Server.h"
#include "../utility/MetadataConfiguration.h"
#include "../onvif_services/EventSource.h"

#include <boost/test/unit_test.hpp>

#include <sstream>

namespace
{
class Logger : public ILogger
{
public:
	Logger() : ILogger(LVL_ERR)
	{
	}
	void Error(const std::string&) const override
	{
	}
	void Warn(const std::string&) const override
	{
	}
	void Info(const std::string&) const override
	{
	}
	void Debug(const std::string&) const override
	{
	}
	void Trace(const std::string&) const override
	{
	}
};

std::string Describe(boost::asio::ip::tcp::iostream& client, unsigned short port)
{
	client.expires_after(std::chrono::seconds(8));
	client.connect("127.0.0.1", std::to_string(port));
	client << "DESCRIBE rtsp://127.0.0.1:" << port
				 << "/Live&LowStream RTSP/1.0\r\nCSeq: 1\r\nAccept: application/sdp\r\n\r\n"
				 << std::flush;
	std::string line;
	std::getline(client, line);
	if (line.find("200 OK") == std::string::npos)
		throw std::runtime_error("DESCRIBE failed: " + line);
	std::size_t length = 0;
	while (std::getline(client, line) && line != "\r")
		if (line.starts_with("Content-Length:"))
			length = std::stoul(line.substr(15));
	std::string body(length, '\0');
	client.read(body.data(), length);
	if (!client)
		throw std::runtime_error("Truncated SDP");
	return body;
}

void PlayMetadata(boost::asio::ip::tcp::iostream& client, unsigned short port, const std::string& sdp)
{
	auto application = sdp.find("m=application");
	auto start = sdp.find("a=control:", application) + std::string("a=control:").size();
	auto control = sdp.substr(start, sdp.find('\r', start) - start);
	auto url = "rtsp://127.0.0.1:" + std::to_string(port) + "/Live&LowStream";
	client << "SETUP " << url << "/" << control
				 << " RTSP/1.0\r\nCSeq: 2\r\nTransport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n\r\n"
				 << std::flush;
	std::string line, session;
	std::getline(client, line);
	if (line.find("200 OK") == std::string::npos)
		throw std::runtime_error("SETUP failed: " + line);
	while (std::getline(client, line) && line != "\r")
		if (line.starts_with("Session:"))
			session = line.substr(9, line.find_first_of(";\r", 9) - 9);
	if (session.empty())
		throw std::runtime_error("No RTSP session");
	client << "PLAY " << url << " RTSP/1.0\r\nCSeq: 3\r\nSession: " << session << "\r\n\r\n" << std::flush;
	std::getline(client, line);
	if (line.find("200 OK") == std::string::npos)
		throw std::runtime_error("PLAY failed: " + line);
	while (std::getline(client, line) && line != "\r")
	{
	}
	// Wait for an actual interleaved RTP/RTCP frame before destroying the server.
	char marker;
	client.get(marker);
	if (marker != '$')
		throw std::runtime_error("No interleaved metadata frame");
}
} // namespace

BOOST_AUTO_TEST_CASE(rtsp_listener_and_active_metadata_prepare_stop_and_restart)
{
	gst_init(nullptr, nullptr);
	auto plugin = gst_element_factory_find("rtponvifmetadatapay");
	bool available = plugin != nullptr;
	if (plugin)
		gst_object_unref(plugin);
	Logger logger;
	for (bool enabled : {false, true})
	{
		if (enabled && !available)
		{
			BOOST_TEST_MESSAGE("Metadata positive case requires the documented metadata-dev image");
			continue;
		}
		osrv::ServerConfigs configs;
		configs.ipv4_address_ = "127.0.0.1";
		configs.rtsp_port_ = "0";
		configs.rtsp_metadata_enabled_ = enabled;
		auto source = std::make_shared<osrv::event::EventSource>(logger);
		boost::asio::ip::tcp::iostream client;
		{
			osrv::rtsp::Server server(&logger, configs, osrv::rtsp::AudioInfo{"PCMU", 64000, 8000}, source);
			if (enabled)
			{
				boost::property_tree::ptree config; config.put("Events", true);
				server.ConfigureProfile("/Live&LowStream", "VideoEncoderToken1", osrv::metadata::Compile(config));
			}
			server.run();
			auto body = Describe(client, server.Port());
			BOOST_TEST((body.find("m=application") != std::string::npos) == enabled);
			if (enabled)
			{
				BOOST_TEST(body.find("VND.ONVIF.METADATA/90000") != std::string::npos);
				PlayMetadata(client, server.Port(), body);
			}
			// The TCP client and prepared media remain alive during server shutdown.
		}
		client.close();
		// Start/stop immediately to cover a stop racing the loop's startup.
		osrv::rtsp::Server restarted(&logger, configs, osrv::rtsp::AudioInfo{"PCMU", 64000, 8000}, source);
		restarted.run();
	}
}
