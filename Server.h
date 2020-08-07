#pragma once

#include "Logger.hpp"

#include <set>

#include <boost/asio.hpp>

namespace {
	using namespace boost::asio;
	const std::string MASTER_ADDR = "127.0.0.1";
	const std::string MASTER_PORT = "8080";
}

//namespace onvif server
namespace osrv
{
	/// Buffer for incoming data.
	using buffer_t = std::array<char, 8192>;
	using bufferSP = std::shared_ptr<buffer_t>;

	using socket_t = ip::tcp::socket;

	class Slave;
	using slaveUP = std::unique_ptr<Slave>;
	using slaves_t = std::set<slaveUP>;

	class ServerBase
	{
	public:
		ServerBase(Logger& l)
			: log_(l)
		{}

		Logger& getLogger()
		{
			return log_;
		}

		void FinalizeSlave(slaveUP s)
		{
			slaves_.erase(s);
		}

	protected:
		slaves_t slaves_;

	private:
		Logger& log_;
	};

	class Slave
	{
	public:
		Slave(socket_t&& s, Logger& log)
			: socket_(std::move(s)),
			log_(log)
		{
		}

		void Start()
		{
			do_read();
		}

		~Slave()
		{
			boost::system::error_code ec;
			using namespace boost::asio;
			socket_.shutdown(socket_base::shutdown_type::shutdown_both, ec);
			socket_.close(ec);

			std::cout << "Destroying a slave" << std::endl;
		}

		Slave(Slave&&) = delete;
		Slave(const Slave&) = delete;

	private:
		void do_read()
		{
			socket_.async_read_some(
				boost::asio::buffer(buffer_),
				[this](boost::system::error_code ec, std::size_t bytes_transferred)
				{

					if (ec)
					{
						log_.Error(std::string("Can't read a client: ") + ec.message());
						//slaves_.erase(slave);
						return;
					}

					std::string info("Read some data has a size: ");
					info += std::to_string(bytes_transferred);
					log_.Info(info);

					do_read();
				}
			);
		}

		buffer_t buffer_;
		socket_t socket_;

		Logger& log_;
	};

	class Server
	{
	public:
		Server(io_context& ctx, Logger& log)
			:ctx_(ctx),
			acceptor_(ctx),
			log_(log)
		{
		}

		//throws exceptions in error
		void run();

	private:
		void do_accept();
		void accept_handler(boost::system::error_code ec,
			boost::asio::ip::tcp::socket socket);

	private:
		io_context& ctx_;
		ip::tcp::acceptor acceptor_;
		buffer_t buffer_;

		slaves_t slaves_;

		Logger& log_;
	};
}
