#include "Server.h"

#include "Simple-Web-Server\server_http.hpp"

namespace osrv
{

	Server::Server(Logger& log)
		: log_(log),
		http_server_instance_(new HttpServer)
	{
		http_server_instance_->config.port = MASTER_PORT;

		//setting up handlers

		// GET-example for the path /info
		// Responds with request-information
		http_server_instance_->resource["^/info$"]["GET"] = [](std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request) {
				std::stringstream stream;
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

		http_server_instance_->default_resource["GET"] = [](std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request)
		{
			response->write(SimpleWeb::StatusCode::client_error_bad_request, "Could not open path " + request->path);
		};
	}

void Server::run()
{
	using namespace std;

	// Start server and receive assigned port when server is listening for requests
	promise<unsigned short> server_port;
	thread server_thread([this, &server_port]() {
		// Start server
		http_server_instance_->start([&server_port](unsigned short port) {
			server_port.set_value(port);
			});
		});

	std::string msg("Server is successfully started on port: ");
	msg += std::to_string(server_port.get_future().get());
	log_.Info(msg);

	server_thread.join();
}

}
