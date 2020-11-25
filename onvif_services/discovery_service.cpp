#include "discovery_service.h"

#include <thread>
#include <memory>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>

#include "../Logger.hpp"

static const Logger* logger_ = nullptr;

namespace ba = boost::asio;


class DiscoveryManager
{
public:
	DiscoveryManager(Logger& logger)
		:logger_(&logger)
	{
		io_ = std::make_shared<ba::io_context>();
		io_work_ = std::make_shared<ba::io_context::work>(*io_);
	}

	void Start()
	{
		if (worker_)
			return;

		socket_ = std::make_shared<ba::ip::udp::socket>(*io_, ba::ip::udp::endpoint(ba::ip::udp::v4(), 3702));
		boost::asio::socket_base::broadcast option(true);
		socket_->set_option(option);

		io_->post([this]() {
				do_receive();
			});

		worker_ = std::make_shared<std::thread>([this]() {io_->run(); });
	}

	void Stop()
	{
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
		logger_->Debug("received: " + std::string(std::begin(data_), std::begin(data_) + length));

		//now do nothing
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
		}

		// may throw an exception if called before @init
		void start()
		{
			if (logger_)
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
