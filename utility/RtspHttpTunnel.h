#pragma once
#include <boost/asio/ip/tcp.hpp>
#include <memory>
#include <string>

namespace osrv
{
class HttpStream;
bool IsRtspHttpTunnel(const std::string& header);
// Omit backend-only redirection; clients must keep the web endpoint they connected to.
std::string RtspHttpTunnelResponse(const std::string& response);
// The owner retains the tracked frontend connection until both pumps finish.
void StartRtspHttpTunnel(HttpStream& stream, std::shared_ptr<void> owner,
												 const boost::asio::ip::tcp::endpoint& endpoint, std::string initial);
} // namespace osrv
