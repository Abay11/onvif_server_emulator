// Transport adapter for Simple-Web-Server v3.1.1 (submodule commit 898b6abd1be568ff9de4390d44288962e3fac337).
// The accept loop is adapted from server_http.hpp/server_https.hpp.
// Copyright (c) 2014-2020 Ole Christian Eidheim. MIT; see LICENSES/Simple-Web-Server-MIT.txt.
// Modified to share HTTP and TLS request/response types and expose the actual transport to WebUI.
#pragma once

#include "HttpServerFwd.h"
#include "Simple-Web-Server/server_http.hpp"
#include "utility/RtspHttpTunnel.h"
#include <boost/asio/ssl.hpp>

namespace osrv
{
// A single stream type keeps SOAP, delayed responses and WebUI handlers shared by both listeners.
class HttpStream
{
public:
	using executor_type = boost::asio::ip::tcp::socket::executor_type;
	HttpStream(boost::asio::io_context& io, std::shared_ptr<boost::asio::ssl::context> tls)
			: context_(std::move(tls)), stream_(io, context_ ? *context_ : PlainContext())
	{
	}
	executor_type get_executor() noexcept
	{
		return stream_.get_executor();
	}
	auto& lowest_layer()
	{
		return stream_.lowest_layer();
	}
	void Prefetch(std::string bytes)
	{
		prefetched_ = std::move(bytes);
	}
	bool Secure() const
	{
		return bool(context_);
	}
	template <class Buffers, class Handler> auto async_read_some(const Buffers& buffers, Handler&& handler)
	{
		if (!prefetched_.empty())
		{
			auto size = boost::asio::buffer_copy(buffers, boost::asio::buffer(prefetched_));
			prefetched_.erase(0, size);
			boost::asio::post(get_executor(), [handler = std::forward<Handler>(handler), size]() mutable {
				handler(boost::system::error_code{}, size);
			});
			return;
		}
		if (context_)
			return stream_.async_read_some(buffers, std::forward<Handler>(handler));
		return stream_.next_layer().async_read_some(buffers, std::forward<Handler>(handler));
	}
	template <class Buffers, class Handler> auto async_write_some(const Buffers& buffers, Handler&& handler)
	{
		if (context_)
			return stream_.async_write_some(buffers, std::forward<Handler>(handler));
		return stream_.next_layer().async_write_some(buffers, std::forward<Handler>(handler));
	}
	template <class Handler> void Handshake(Handler&& handler)
	{
		stream_.async_handshake(boost::asio::ssl::stream_base::server, std::forward<Handler>(handler));
	}

private:
	static boost::asio::ssl::context& PlainContext()
	{
		static boost::asio::ssl::context context(boost::asio::ssl::context::tls_server);
		return context;
	}
	std::string prefetched_;
	std::shared_ptr<boost::asio::ssl::context> context_;
	boost::asio::ssl::stream<boost::asio::ip::tcp::socket> stream_;
};
} // namespace osrv

namespace SimpleWeb
{
template <> class Server<osrv::HttpStream> : public ServerBase<osrv::HttpStream>
{
public:
	Server() : ServerBase(8080)
	{
	}
	static bool IsSecure(const std::shared_ptr<Request>& request)
	{
		auto connection = request->connection.lock();
		return connection && connection->socket->Secure();
	}
	std::shared_ptr<asio::ssl::context> tls_context;
	asio::ip::tcp::endpoint rtsp_tunnel_endpoint;

protected:
	// Inspect the initial HTTP header before the generic server waits for a POST body.
	// Tunnel POST bodies are open-ended base64 RTSP, not ordinary HTTP request content.
	void read_initial(const std::shared_ptr<Session>& session)
	{
		if (!rtsp_tunnel_endpoint.port())
		{
			read(session);
			return;
		}
		auto bytes = std::make_shared<asio::streambuf>(65536);
		session->connection->set_timeout(config.timeout_request);
		asio::async_read_until(*session->connection->socket, *bytes, "\r\n\r\n",
			[this, session, bytes](const error_code& ec, size_t header_size) {
				auto lock = session->connection->handler_runner->continue_lock();
				if (!lock)
					return;
				session->connection->cancel_timeout();
				if (ec)
				{
					if (on_error) on_error(session->request, ec);
					return;
				}
				std::string initial(asio::buffers_begin(bytes->data()), asio::buffers_end(bytes->data()));
				if (osrv::IsRtspHttpTunnel(initial.substr(0, header_size)))
				{
					// Keep the connection registered so stop() cancels active tunnels too.
					osrv::StartRtspHttpTunnel(*session->connection->socket, session->connection,
							rtsp_tunnel_endpoint, std::move(initial));
					return;
				}
				session->connection->socket->Prefetch(std::move(initial));
				read(session);
			});
	}
	void accept() override
	{
		auto connection = create_connection(*io_service, tls_context);
		acceptor->async_accept(connection->socket->lowest_layer(), [this, connection](const error_code& ec) {
			auto lock = connection->handler_runner->continue_lock();
			if (!lock)
				return;
			if (ec != error::operation_aborted)
				accept();
			auto session = std::make_shared<Session>(config.max_request_streambuf_size, connection);
			if (ec)
			{
				if (on_error)
					on_error(session->request, ec);
				return;
			}
			error_code option_error;
			connection->socket->lowest_layer().set_option(asio::ip::tcp::no_delay(true), option_error);
			if (!connection->socket->Secure())
			{
				read_initial(session);
				return;
			}
			connection->set_timeout(config.timeout_request);
			connection->socket->Handshake([this, session](const error_code& error) {
				session->connection->cancel_timeout();
				auto lock = session->connection->handler_runner->continue_lock();
				if (!lock)
					return;
				if (!error)
					read_initial(session);
				else if (on_error)
					on_error(session->request, error);
			});
		});
	}
};
} // namespace SimpleWeb
