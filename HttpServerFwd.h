#pragma once

namespace boost::asio
{
class executor;

#if !defined(BOOST_ASIO_BASIC_STREAM_SOCKET_FWD_DECL)
#define BOOST_ASIO_BASIC_STREAM_SOCKET_FWD_DECL

// Forward declaration with defaulted arguments.
template <typename Protocol, typename Executor = executor> class basic_stream_socket;

#endif // !defined(BOOST_ASIO_BASIC_STREAM_SOCKET_FWD_DECL)

namespace ip
{
class tcp;
}
} // namespace boost::asio

namespace SimpleWeb
{
template <typename T> class Server;
}

namespace osrv
{
using socket_t = boost::asio::basic_stream_socket<boost::asio::ip::tcp>;
using HttpServer = SimpleWeb::Server<socket_t>;
} // namespace osrv
