#include "Server.h"
#include "Slave.h"

#include "Simple-Web-Server\server_http.hpp"

namespace osrv
{

void Server::run()
{
	using HttpServer = SimpleWeb::Server<SimpleWeb::HTTP>;
	using namespace std;

	HttpServer server;
	server.config.port = MASTER_PORT;

	// GET-example for the path /info
	 // Responds with request-information
	server.resource["^/info$"]["GET"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
		stringstream stream;
		stream << "<h1>Request from " << request->remote_endpoint().address().to_string() << ":" << request->remote_endpoint().port() << "</h1>";

		stream << request->method << " " << request->path << " HTTP/" << request->http_version;

		stream << "<h2>Query Fields</h2>";
		auto query_fields = request->parse_query_string();
		for (auto &field : query_fields)
			stream << field.first << ": " << field.second << "<br>";

		stream << "<h2>Header Fields</h2>";
		for (auto &field : request->header)
			stream << field.first << ": " << field.second << "<br>";

		response->write(stream);
	};

	server.default_resource["GET"] = [](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
		response->write(SimpleWeb::StatusCode::client_error_bad_request, "Could not open path " + request->path);
	};

	// Start server and receive assigned port when server is listening for requests
	promise<unsigned short> server_port;
	thread server_thread([&server, &server_port]() {
		// Start server
		server.start([&server_port](unsigned short port) {
			server_port.set_value(port);
			});
		});


	std::string msg("Server is successfully started on port: ");
	msg += std::to_string(server_port.get_future().get());
	log_.Info(msg);

	server_thread.join();
}

void Server::do_accept()
{
	using namespace std::placeholders;
	acceptor_.async_accept(std::bind(&Server::accept_handler, this, _1, _2));
}

void Server::accept_handler(boost::system::error_code ec,
			boost::asio::ip::tcp::socket socket)
{
			// Check whether the server was stopped by a signal before this
			// completion handler had a chance to run.
			if (!acceptor_.is_open())
			{
				return;
			}

			if (ec)
			{
				log_.Error("Can't accept a new client");
				return;
			}

			do_accept();

			auto it = slaves_.insert(std::make_shared<Slave>(std::move(socket), *this));
			auto& slave = *(it.first);
			slave->Start();
}

}
