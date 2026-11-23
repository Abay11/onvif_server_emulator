#pragma once
#include <boost/asio/ssl/context.hpp>
#include <memory>
#include <string>

namespace utility
{
// Empty paths select an ephemeral development certificate; custom paths are PEM files.
std::shared_ptr<boost::asio::ssl::context> MakeTlsContext(const std::string& certificate,
																													const std::string& private_key,
																													const std::string& advertised_ipv4);
} // namespace utility
