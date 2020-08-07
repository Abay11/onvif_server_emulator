#pragma once

#include <array>
#include <set>
#include <memory>

#include <boost/asio/ip/tcp.hpp>


	/// Buffer for incoming data.
	using buffer_t = std::array<char, 8192>;
	using bufferSP = std::shared_ptr<buffer_t>;

	using socket_t = boost::asio::ip::tcp::socket;

	namespace osrv
	{
		class Slave;
		using slaveSP = std::shared_ptr<Slave>;
		using slaves_t = std::set<slaveSP>;
	}