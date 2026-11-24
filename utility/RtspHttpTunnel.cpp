#include "RtspHttpTunnel.h"
#include "../HttpServer.h"
#include <array>
#include <sstream>

namespace osrv
{
bool IsRtspHttpTunnel(const std::string& header)
{
	std::istringstream input(header);
	std::string method, path, query, version;
	SimpleWeb::CaseInsensitiveMultimap fields;
	if (!SimpleWeb::RequestMessage::parse(input, method, path, query, version, fields))
		return false;
	return (method == "GET" || method == "POST") && fields.count("x-sessioncookie") == 1 &&
				 !fields.find("x-sessioncookie")->second.empty();
}

std::string RtspHttpTunnelResponse(const std::string& response)
{
	std::string result;
	size_t start = 0;
	while (start < response.size())
	{
		auto end = response.find("\r\n", start);
		if (end == std::string::npos)
			return result + response.substr(start);
		if (end == start)
			return result + response.substr(start);
		auto line = response.substr(start, end - start);
		auto colon = line.find(':');
		if (colon == std::string::npos || !SimpleWeb::case_insensitive_equal(line.substr(0, colon), "x-server-ip-address"))
			result += line + "\r\n";
		start = end + 2;
	}
	return result;
}

namespace
{
class Tunnel : public std::enable_shared_from_this<Tunnel>
{
	HttpStream& frontend_;
	std::shared_ptr<void> owner_;
	boost::asio::ip::tcp::socket backend_;
	std::array<char, 16384> upstream_, downstream_;
	std::string initial_;
	const bool get_;
	boost::asio::streambuf response_header_{65536};
	bool stopped_ = false;

	void Stop()
	{
		if (stopped_)
			return;
		stopped_ = true;
		boost::system::error_code ignored;
		frontend_.lowest_layer().cancel(ignored);
		frontend_.lowest_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored);
		frontend_.lowest_layer().close(ignored);
		backend_.cancel(ignored);
		backend_.close(ignored);
	}
	void Upstream()
	{
		frontend_.async_read_some(
				boost::asio::buffer(upstream_), [self = shared_from_this()](const boost::system::error_code& ec, size_t size) {
					if (ec)
					{
						self->Stop();
						return;
					}
					boost::asio::async_write(self->backend_, boost::asio::buffer(self->upstream_.data(), size),
																	 [self](const boost::system::error_code& ec, size_t) {
																		 if (ec)
																			 self->Stop();
																		 else
																			 self->Upstream();
																	 });
				});
	}
	void Downstream()
	{
		backend_.async_read_some(boost::asio::buffer(downstream_), [self = shared_from_this()](
																																	 const boost::system::error_code& ec, size_t size) {
			if (ec)
			{
				self->Stop();
				return;
			}
			boost::asio::async_write(self->frontend_, boost::asio::buffer(self->downstream_.data(), size),
															 [self](const boost::system::error_code& ec, size_t) {
																 if (ec)
																	 self->Stop();
																 else
																	 self->Downstream();
															 });
		});
	}

	void ResponseHeader()
	{
		boost::asio::async_read_until(backend_, response_header_, "\r\n\r\n",
																	[self = shared_from_this()](const boost::system::error_code& ec, size_t) {
																		if (ec)
																		{
																			self->Stop();
																			return;
																		}
																		std::string bytes(boost::asio::buffers_begin(self->response_header_.data()),
																											boost::asio::buffers_end(self->response_header_.data()));
																		self->response_header_.consume(self->response_header_.size());
																		self->initial_ = RtspHttpTunnelResponse(bytes);
																		boost::asio::async_write(self->frontend_, boost::asio::buffer(self->initial_),
																														 [self](const boost::system::error_code& ec, size_t) {
																															 if (ec)
																															 {
																																 self->Stop();
																																 return;
																															 }
																															 self->initial_.clear();
																															 self->Downstream();
																														 });
																	});
	}

public:
	Tunnel(HttpStream& stream, std::shared_ptr<void> owner, std::string initial)
			: frontend_(stream), owner_(std::move(owner)), backend_(stream.get_executor()), initial_(std::move(initial)),
				get_(initial_.starts_with("GET "))
	{
	}
	void Start(const boost::asio::ip::tcp::endpoint& endpoint)
	{
		backend_.async_connect(endpoint, [self = shared_from_this()](const boost::system::error_code& ec) {
			if (ec)
			{
				self->Stop();
				return;
			}
			boost::asio::async_write(self->backend_, boost::asio::buffer(self->initial_),
															 [self](const boost::system::error_code& ec, size_t) {
																 if (ec)
																 {
																	 self->Stop();
																	 return;
																 }
																 self->initial_.clear();
																 self->Upstream();
																 if (self->get_)
																	 self->ResponseHeader();
																 else
																	 self->Downstream();
															 });
		});
	}
};
} // namespace

void StartRtspHttpTunnel(HttpStream& stream, std::shared_ptr<void> owner,
												 const boost::asio::ip::tcp::endpoint& endpoint, std::string initial)
{
	std::make_shared<Tunnel>(stream, std::move(owner), std::move(initial))->Start(endpoint);
}
} // namespace osrv
