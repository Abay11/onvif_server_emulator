#include "discovery_service.h"

#include "../Logger.hpp"
#include "../utility/XmlParser.h"

#include <thread>
#include <memory>
#include <fstream>
#include <algorithm>
#include <regex>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio.hpp>

#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/ptree.hpp>

static const Logger* logger_ = nullptr;

static std::string CONFIGS_PATH; //will be init with the service initialization
static const std::string DISCOVERY_RESPONSES_FILE = "discovery_service_responses/probe_match.responses";


namespace ba = boost::asio;

class DiscoveryManager
{
public:
	DiscoveryManager(Logger& logger, std::string&& response)
		:logger_(&logger),
		response_(std::move(response)),
		remote_endpoint_()
	{
		io_ = std::make_shared<ba::io_context>();
		io_work_ = std::make_shared<ba::io_context::work>(*io_);
	}

	void Start()
	{
		if (worker_)
			return;

		socket_ = std::make_shared<ba::ip::udp::socket>(*io_);
		socket_->open(ba::ip::udp::v4());
		socket_->set_option(ba::socket_base::reuse_address(true));
		socket_->set_option(ba::socket_base::broadcast(true));
		socket_->set_option(ba::ip::multicast::join_group(ba::ip::address::from_string("239.255.255.250")));

		boost::system::error_code ec;
		socket_->bind(ba::ip::udp::endpoint(ba::ip::udp::v4(), 3702), ec);
		if (ec)
		{
			logger_->Error("Can't bind the Discovery's socket to the port 3702: " + ec.message());
		}

		io_->post([this]() {
				do_receive();
			});

		worker_ = std::make_shared<std::thread>([this]() {io_->run(); });
	}

	void Stop()
	{
		if (!io_work_)
			return;

		io_work_.reset();

		try
		{
			worker_->join();
		}
		catch (const std::exception&)
		{
		}
	}

private:
	void do_receive()
	{
		socket_->async_receive_from(ba::buffer(data_, max_length), remote_endpoint_,
			[this](boost::system::error_code ec, std::size_t bytes_recvd) {
				logger_->Info("Received a probe from: "
					+ remote_endpoint_.address().to_string() + ":" + std::to_string(remote_endpoint_.port()));

				if (!ec && bytes_recvd > 0)
				{
					do_send(bytes_recvd);
				}
				else
				{
					do_receive();
				}
			});
	}

	void do_send(std::size_t length)
	{
		std::string probe_msg(std::begin(data_), std::begin(data_) + length);
		logger_->Debug("A probe's message: " + probe_msg);

		const auto relatesTo = osrv::discovery::utility::extract_message_id(probe_msg);

		if (relatesTo.empty())
		{
			logger_->Error("Probe's messageID is empty! Probe match dropped!");
		}
		else
		{
			response_ = osrv::discovery::utility::prepare_response(osrv::discovery::utility::generate_uuid(),
				relatesTo, std::move(response_));

			socket_->async_send_to(ba::buffer(response_), remote_endpoint_,
				[this](const boost::system::error_code& ec, std::size_t bytes_transferred) {
					if (ec)
					{
						logger_->Error("Something went wrong while sending a response to the Probe: " + ec.message());
					}
					else if (bytes_transferred != response_.size())
					{
						logger_->Warn("Sending message length on the Probe not match actual required size Probe!");
					}

					logger_->Info("Response a probe match to: "
						+ remote_endpoint_.address().to_string() + ":" + std::to_string(remote_endpoint_.port()));
				});
		}
		
		io_->post([this]() { do_receive(); });
	}

private:
Logger* logger_ = nullptr;

std::string response_;

std::shared_ptr<std::thread> worker_;
std::shared_ptr<ba::io_context> io_;
std::shared_ptr<ba::io_context::work> io_work_;

std::shared_ptr<ba::ip::udp::socket> socket_;
ba::ip::udp::endpoint remote_endpoint_;
enum { max_length = 4096};
char data_[max_length];
};

std::shared_ptr<DiscoveryManager> discovery_manager_;

namespace osrv
{
	namespace discovery
	{
		void init_service(const std::string& configs_path, Logger& logger)
		{
			logger_ = &logger;

			logger_->Info("Initiating Discovery Service");

			CONFIGS_PATH = configs_path;

			std::ifstream ifs(CONFIGS_PATH + DISCOVERY_RESPONSES_FILE);
			if (!ifs.is_open())
			{
				throw std::runtime_error("Can't open: " + DISCOVERY_RESPONSES_FILE);
			}
	
			std::string response;
			response.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());

			discovery_manager_ = std::make_shared<DiscoveryManager>(logger, std::move(response));
		}

		void start()
		{
			if (!logger_)
				throw std::runtime_error("Discovery Service should be initialized before starting!");

			logger_->Info("Starting Discovery Service");
			
			discovery_manager_->Start();
		}
		
		void stop()
		{
			if (!logger_)
				return;

			logger_->Info("Stopping Discovery Service");

			discovery_manager_->Stop();

			logger_ = nullptr;
		}
			
		namespace utility
		{
			std::string extract_message_id(const std::string& probe_msg)
			{
				namespace pt = boost::property_tree;
				std::istringstream is(probe_msg);
				pt::ptree probe_tree;
				pt::xml_parser::read_xml(is, probe_tree);

				return exns::find_hierarchy("Envelope.Header.MessageID", probe_tree);
			}

			std::string prepare_response(const std::string& messageID, const std::string& relatesTo, std::string&& response)
			{
				//TODO: replace MessageID
				std::regex re_msg_id("(<.*MessageID>)(.*)(</.*MessageID>)");
				response = std::regex_replace(response, re_msg_id, "$1" + messageID + "$3");

				// replace RelatesTo uuid
				std::regex re_rel_to("(<.*RelatesTo>)(.*)(</.*RelatesTo>)");
				return std::regex_replace(response, re_rel_to, "$1" + relatesTo + "$3");
			}
			
			std::string generate_uuid(std::string uuid)
			{
				if (uuid.empty())
					return uuid;

				// beginning part will be fixed, we just will increment the counter from 0 and will be
				// replace the last characters in the ending
				//std::string uuid = "urn:uuid:1419d68a-1dd2-11b2-a105-000000000000";

				static int ID = 0;
				std::string str_id = std::to_string(ID++);
				uuid.replace(uuid.length() - str_id.length(), //start pos
					str_id.length(), //num of symbols to replace
					str_id); //new value

				return uuid;
			}
		}
		
	}
}
