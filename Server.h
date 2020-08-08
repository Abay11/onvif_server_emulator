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
	class Server
	{
	public:
		Server(Logger& log);
		Server(const Logger&) = delete;
		Server(Logger&&) = delete;

		//throws exceptions in error
		void run();

	private:

	private:
		Logger& log_;

		std::shared_ptr<HttpServer> http_server_instance_ = nullptr;
	};
}
