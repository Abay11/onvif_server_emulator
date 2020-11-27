#include "discovery_service.h"

#include <thread>
#include <memory>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio.hpp>

#include "../Logger.hpp"

static const Logger* logger_ = nullptr;

namespace ba = boost::asio;


class DiscoveryManager
{
public:
	DiscoveryManager(Logger& logger)
		:logger_(&logger),
		sender_endpoint_()
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
		socket_->async_receive_from(ba::buffer(data_, max_length), sender_endpoint_,
			[this](boost::system::error_code ec, std::size_t bytes_recvd) {
				logger_->Info("Received a probe from: "
					+ sender_endpoint_.address().to_string() + ":" + std::to_string(sender_endpoint_.port()));

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
		logger_->Debug("A probe's message: " + std::string(std::begin(data_), std::begin(data_) + length));
		io_->post([this]() { do_receive(); });
	}

private:
Logger* logger_ = nullptr;

std::shared_ptr<std::thread> worker_;
std::shared_ptr<ba::io_context> io_;
std::shared_ptr<ba::io_context::work> io_work_;

std::shared_ptr<ba::ip::udp::socket> socket_;
ba::ip::udp::endpoint sender_endpoint_;
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

			logger_->Info("Init Discovery Service");

			discovery_manager_ = std::make_shared<DiscoveryManager>(logger);
		}

		// may throw an exception if called before @init
		void start()
		{
			if (!logger_)
				throw std::runtime_error("Discovery Service should be initialized before starting!");

			logger_->Info("Starting Discovery Service");
			
			discovery_manager_->Start();
		}
		
		void stop()
		{
			if (logger_)
				return;

			logger_->Info("Stopping Discovery Service");

			discovery_manager_->Stop();
		}
	}
}
