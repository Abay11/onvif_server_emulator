#pragma once

#include "Types.inl"
#include "Logger.hpp"

#include <set>

#include <boost/asio.hpp>
#include <memory>

namespace {
	using namespace boost::asio;
	const std::string MASTER_ADDR = "127.0.0.1";
	const unsigned short MASTER_PORT = 8080;
}

//namespace onvif server
namespace osrv
{

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

		void FinalizeSlave(slaveSP s)
		{
			slaves_.erase(s);
		}

	protected:
		slaves_t slaves_;

		Logger& log_;
	};

		class Server : public ServerBase
	{
	public:
		Server(io_context& ctx, Logger& log)
			: ServerBase(log),
			ctx_(ctx),
			acceptor_(ctx)
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
	};
}
