#include "discovery_service.h"

#include "../Logger.h"
#include "../Server.h"
#include <boost/property_tree/json_parser.hpp>
#include <cstdlib>
#include "../utility/XmlParser.h"

#include <fstream>
#include <memory>
#include <thread>

#include <boost/asio.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

static const ILogger* logger_ = nullptr;

static std::string CONFIGS_PATH; // will be init with the service initialization
static const std::string DISCOVERY_RESPONSE_FILE = "responses/ProbeMatch.response";

namespace ba = boost::asio;

namespace
{
std::string discovery_value(const boost::property_tree::ptree& tree, std::string_view path)
{
	const auto elements = exns::find_hierarchy_elements(path, tree);
	return elements.empty() ? std::string{} : elements.front()->second.get_value<std::string>();
}

bool replace_element(boost::property_tree::ptree& tree, std::string_view element, const std::string& value)
{
	bool replaced = false;
	for (auto& [name, child] : tree)
	{
		if (name.substr(name.find_last_of(':') + 1) == element)
		{
			child.put_value(value);
			replaced = true;
		}
		else
			replaced = replace_element(child, element, value) || replaced;
	}
	return replaced;
}
} // namespace

class DiscoveryManager
{
public:
	DiscoveryManager(ILogger& logger, std::string response, std::string interface_ipv4)
		: logger_(&logger), response_(std::move(response)), interface_ipv4_(std::move(interface_ipv4)), socket_(io_)
	{
	}

	void Start()
	{
		if (worker_.joinable())
			return;
		io_.restart();
		socket_.open(ba::ip::udp::v4());
		socket_.set_option(ba::socket_base::reuse_address(true));
		socket_.bind(ba::ip::udp::endpoint(ba::ip::udp::v4(), 3702));
		const auto group = ba::ip::make_address_v4("239.255.255.250");
		if (interface_ipv4_.empty())
			socket_.set_option(ba::ip::multicast::join_group(group));
		else
			socket_.set_option(ba::ip::multicast::join_group(group, ba::ip::make_address_v4(interface_ipv4_)));
		logger_->Info("WS-Discovery listening on UDP 3702, multicast interface: " +
			(interface_ipv4_.empty() ? std::string("system default") : interface_ipv4_));
		do_receive();
		worker_ = std::thread([this]() { io_.run(); });
	}

	void Stop()
	{
		if (worker_.joinable())
		{
			ba::post(io_, [this]() { socket_.close(); });
			worker_.join();
		}
		else
		{
			boost::system::error_code ec;
			socket_.close(ec);
		}
	}

	~DiscoveryManager() { Stop(); }

private:
	void do_receive()
	{
		socket_.async_receive_from(ba::buffer(data_), remote_endpoint_,
			[this](boost::system::error_code ec, std::size_t length) {
				if (ec == ba::error::operation_aborted || !socket_.is_open())
					return;
				if (!ec)
				{
					try
					{
						auto tree = exns::to_ptree(std::string(data_, length));
						const auto types = osrv::discovery::utility::extract_types(tree);
						const auto id = osrv::discovery::utility::extract_message_id(tree);
						if (!id.empty() && (types.empty() || types.find("NetworkVideoTransmitter") != std::string::npos ||
							types.find("NetworkVideoDisplay") != std::string::npos || types.find("Device") != std::string::npos))
						{
							// Only a Probe may trigger a response (not arbitrary WS-Discovery messages).
							if (!discovery_value(tree, "Envelope.Header.Action").ends_with("/Probe") ||
								exns::find_hierarchy_elements("Envelope.Body.Probe", tree).empty())
							{
								do_receive();
								return;
							}
							pending_response_ = osrv::discovery::utility::prepare_response(
								osrv::discovery::utility::generate_uuid(), id, std::string(response_));
							socket_.async_send_to(ba::buffer(pending_response_), remote_endpoint_,
								[this](boost::system::error_code error, std::size_t) {
									if (error && error != ba::error::operation_aborted)
										logger_->Warn("WS-Discovery response failed: " + error.message());
									if (socket_.is_open())
										do_receive();
								});
							return;
						}
					}
					catch (const std::exception& error)
					{
						logger_->Warn("Ignoring malformed WS-Discovery datagram: " + std::string(error.what()));
					}
				}
				do_receive();
			});
	}

	ILogger* logger_;
	const std::string response_;
	const std::string interface_ipv4_;
	std::string pending_response_;
	ba::io_context io_;
	ba::ip::udp::socket socket_;
	ba::ip::udp::endpoint remote_endpoint_;
	std::thread worker_;
	char data_[4096];
};

std::shared_ptr<DiscoveryManager> discovery_manager_;

namespace osrv
{
namespace discovery
{
void init_service(const std::string& configs_path, ILogger& logger, const ServerConfigs& server_configs)
{
	logger_ = &logger;

	logger_->Info("Initiating Discovery Service");

	CONFIGS_PATH = configs_path;
	boost::property_tree::ptree config;
	boost::property_tree::read_json(CONFIGS_PATH + "discovery.config", config);
	discovery_manager_.reset();
	if (!config.get<bool>("enabled", true))
	{
		logger_->Info("WS-Discovery disabled");
		return;
	}
	auto interface_ipv4 = config.get<std::string>("interface_ipv4", "");
	if (const char* value = std::getenv("ONVIF_DISCOVERY_INTERFACE_IPV4"))
		interface_ipv4 = value;
	if (!interface_ipv4.empty())
	{
		const auto interface = ba::ip::make_address_v4(interface_ipv4);
		if (interface.is_unspecified() || interface.is_multicast() || interface.to_uint() == 0xffffffff)
			throw std::runtime_error("discovery.interface_ipv4 must identify a local unicast IPv4 interface");
	}

	std::ifstream ifs(CONFIGS_PATH + DISCOVERY_RESPONSE_FILE);
	if (!ifs.is_open())
	{
		throw std::runtime_error("Can't open: " + DISCOVERY_RESPONSE_FILE);
	}

	std::string response;
	response.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());

	response = utility::set_xaddrs(response, server_configs.HttpAddress() + "onvif/device_service");
	discovery_manager_ = std::make_shared<DiscoveryManager>(logger, std::move(response), interface_ipv4);
}

void start()
{
	if (!logger_)
		throw std::runtime_error("Discovery Service should be initialized before starting!");

	logger_->Info("Starting Discovery Service");

	if (discovery_manager_)
		discovery_manager_->Start();
}

void stop()
{
	if (!logger_)
		return;

	logger_->Info("Stopping Discovery Service");

	if (discovery_manager_)
		discovery_manager_->Stop();

	logger_ = nullptr;
}

namespace utility
{
std::string set_xaddrs(const std::string& response, const std::string& address)
{
	boost::property_tree::ptree tree;
	std::istringstream input(response);
	boost::property_tree::read_xml(input, tree);
	if (!replace_element(tree, "XAddrs", address))
		throw std::runtime_error("Discovery response template has no XAddrs element");
	std::ostringstream output;
	boost::property_tree::write_xml(output, tree);
	return output.str();
}

std::string extract_types(const boost::property_tree::ptree& tree)
{
	return discovery_value(tree, "Envelope.Body.Probe.Types");
}

std::string extract_message_id(const boost::property_tree::ptree& tree)
{
	return discovery_value(tree, "Envelope.Header.MessageID");
}

std::string prepare_response(const std::string& messageID, const std::string& relatesTo, std::string&& response)
{
	boost::property_tree::ptree tree;
	std::istringstream input(response);
	boost::property_tree::read_xml(input, tree);
	replace_element(tree, "MessageID", messageID);
	replace_element(tree, "RelatesTo", relatesTo);
	std::ostringstream output;
	boost::property_tree::write_xml(output, tree);
	return output.str();
}

std::string generate_uuid(std::string uuid)
{
	if (uuid.empty())
		return uuid;

	// beginning part will be fixed, we just will increment the counter from 0 and will be
	// replace the last characters in the ending
	// std::string uuid = "urn:uuid:1419d68a-1dd2-11b2-a105-000000000000";

	static int ID = 0;
	std::string str_id = std::to_string(ID++);
	uuid.replace(uuid.length() - str_id.length(), // start pos
							 str_id.length(),									// num of symbols to replace
							 str_id);													// new value

	return uuid;
}
} // namespace utility

} // namespace discovery
} // namespace osrv
