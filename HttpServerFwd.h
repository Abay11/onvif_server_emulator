#pragma once

namespace SimpleWeb
{
template <typename T> class Server;
}

namespace osrv
{
class HttpStream;
using socket_t = HttpStream;
using HttpServer = SimpleWeb::Server<socket_t>;
} // namespace osrv
