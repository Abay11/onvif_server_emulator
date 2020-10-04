#include "Server.h"
#include "../onvif_services/device_service.h"
#include "../onvif_services/media_service.h"

#include "Simple-Web-Server\server_http.hpp"

#include <string>

//TODO: make more general the way of getting configs path
static const std::string DEVICE_CONFIGS = "../server_configs/device.config";
static const std::string MEDIA_CONFIGS = "../server_configs/";

static char RTSP_PORT[] = "8554";

namespace osrv
{

	Server::Server(Logger& log)
		: log_(log),
		http_server_instance_(new HttpServer),
		rtspServer_(new rtsp::Server(&log, RTSP_PORT))
	{
		http_server_instance_->config.port = MASTER_PORT;
	
		http_server_instance_->default_resource["GET"] = [](std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request)
		{
			response->write(SimpleWeb::StatusCode::client_error_bad_request, "Could not open path " + request->path);
		};
		
		http_server_instance_->default_resource["POST"] = [](std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request)
		{
			response->write(SimpleWeb::StatusCode::client_error_bad_request, "Bad request");
		};

		device::init_service(*http_server_instance_, DEVICE_CONFIGS, log);
		media::init_service(*http_server_instance_, MEDIA_CONFIGS, log);

	}

	Server::~Server()
	{
		delete rtspServer_;
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

	rtspServer_->run();

	std::string msg("Server is successfully started on port: ");
	msg += std::to_string(server_port.get_future().get());
	log_.Info(msg);

	server_thread.join();
}

}
