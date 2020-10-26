#pragma once

#include "Types.inl"
#include "Logger.hpp"
#include "RtspServer.h"
#include "utility/HttpDigestHelper.h"

#include <memory>

namespace {
	const std::string MASTER_ADDR = "127.0.0.1";
	const unsigned short MASTER_PORT = 8080;
}

//namespace onvif server
namespace osrv
{
	class Server
	{
	public:
		Server(std::string /*configs_dir*/, Logger& log);
		Server(const Logger&) = delete;
		Server(Logger&&) = delete;
		~Server();

		//throws exceptions in error
		void run();

	private:

	private:
		Logger& log_;

		std::shared_ptr<HttpServer> http_server_instance_ = nullptr;

		DigestSessionSP digest_session_;

		rtsp::Server* rtspServer_;
	};
}
