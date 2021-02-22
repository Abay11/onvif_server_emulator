#pragma once

#include <array>
#include <memory>

#include <boost/asio/ip/tcp.hpp>

#include <map>
#include <string>

	using socket_t = boost::asio::ip::tcp::socket;
	
	//SimpleWeb has an alias HTTP
	namespace SimpleWeb
	{
		using HTTP = socket_t;

		template <typename T>
		class Server;
	}

	namespace osrv
	{
		using HttpServer = SimpleWeb::Server<SimpleWeb::HTTP>;

		using StringsMap = std::map<std::string, std::string>;

		namespace auth
		{
			struct UserAccount;
		}
	}

	namespace utility
	{
		namespace digest
		{
			class IDigestSession;
		}
	}

	using UsersList_t = std::vector<osrv::auth::UserAccount>;		
	using DigestSessionSP = std::shared_ptr<utility::digest::IDigestSession>;
