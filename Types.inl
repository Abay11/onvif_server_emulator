#pragma once

#include <array>
#include <set>
#include <memory>

#include <boost/asio/ip/tcp.hpp>

#include <map>
#include <string>

	/// Buffer for incoming data.
	using buffer_t = std::array<char, 8192>;
	using bufferSP = std::shared_ptr<buffer_t>;

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
	}