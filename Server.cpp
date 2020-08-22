#include "Server.h"
#include "../onvif_services/device_service.h"

#include "Simple-Web-Server\server_http.hpp"

//TODO: make more general the way of getting configs path
const std::string DEVICE_CONFIGS = "../server_configs/device.config";

namespace osrv
{

	Server::Server(Logger& log)
		: log_(log),
		http_server_instance_(new HttpServer)
	{
		http_server_instance_->config.port = MASTER_PORT;
	
		http_server_instance_->default_resource["GET"] = [](std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request)
		{
			response->write(SimpleWeb::StatusCode::client_error_bad_request, "Could not open path " + request->path);
		};

		device::init_service(*http_server_instance_, DEVICE_CONFIGS, log);
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
