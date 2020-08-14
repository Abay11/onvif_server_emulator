#include "device_service.h"

#include "../Logger.hpp"

#include "../Simple-Web-Server/server_http.hpp"

#include <boost/property_tree/xml_parser.hpp>

static Logger* log_ = nullptr;

namespace osrv
{
	namespace device
	{
		void GetSystemDateAndTime(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request)
		{
			log_->Debug("Handle GetSystemDateAndTime");

			*response << "HTTP/1.1 200 OK\r\nContent-Length: " << 0 << "\r\n\r\n";
		}
		
		void DeviceServiceHandler(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request)
		{
			auto content = request->content.string();
			if(content.find("GetSystemDateAndTime") != std::string::npos)
			{
				GetSystemDateAndTime(response, request);
			}
			else
			{
				log_->Debug("Not found appropriate handler");
				*response << "HTTP/1.1 500 Server error\r\nContent-Length: " << 0 << "\r\n\r\n";
			}
		}

		void init_service(HttpServer& srv, Logger& logger)
		{
			log_ = &logger;

			log_->Debug("Init Device service");

			srv.resource["/onvif/device_service"]["POST"] = DeviceServiceHandler;
		}

	}
}