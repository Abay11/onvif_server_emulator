#include "device_service.h"

#include "../Logger.hpp"
#include "../utility/XmlParser.h"

#include "../Simple-Web-Server/server_http.hpp"

#include <boost/property_tree/xml_parser.hpp>

#include <map>

static Logger* log_ = nullptr;

//List of implemented methods
const std::string GetCapabilities = "GetCapabilities";
const std::string GetScopes = "GetScopes";
const std::string GetSystemDateAndTime = "GetSystemDateAndTime";

namespace osrv
{
	namespace device
	{
		using handler_t = void(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request);
		static std::map<std::string, handler_t*> handlers;

		void GetCapabilitiesHandler(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request)
		{
			log_->Debug("Handle GetCapabilities");

			*response << "HTTP/1.1 200 OK\r\nContent-Length: " << 0 << "\r\n\r\n";
		}
		
		void GetScopesHandler(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request)
		{
			log_->Debug("Handle GetScopes");

			*response << "HTTP/1.1 200 OK\r\nContent-Length: " << 0 << "\r\n\r\n";
		}

		void GetSystemDateAndTimeHandler(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request)
		{
			log_->Debug("Handle GetSystemDateAndTime");

			*response << "HTTP/1.1 200 OK\r\nContent-Length: " << 0 << "\r\n\r\n";
		}
		
		//DEFAULT HANDLER
		void DeviceServiceHandler(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request)
		{
			/*
				//DeviceService's request example

				<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope">
					<s:Body xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
						xmlns:xsd="http://www.w3.org/2001/XMLSchema">
						<GetSystemDateAndTime xmlns="http://www.onvif.org/ver10/device/wsdl"/>
					</s:Body>
				</s:Envelope>
			*/
			
			//extract requested method
			std::string method;
			auto content = request->content.string();
			std::istringstream is(content);
			namespace pt = boost::property_tree;
			pt::ptree* tree = new exns::Parser();
			try
			{
				pt::xml_parser::read_xml(is, *tree);
				auto* ptr = static_cast<exns::Parser*>(tree);
				method = static_cast<exns::Parser*>(tree)->___getMethod();
			}
			catch (const pt::xml_parser_error& e)
			{
				log_->Error(e.what());
			}

			auto it = handlers.find(method);

			//handle methods
			if (it != handlers.end())
			{
				it->second(response, request);
			}
			else
			{
				log_->Debug("Not found appropriate handler for " + method);
				*response << "HTTP/1.1 500 Server error\r\nContent-Length: " << 0 << "\r\n\r\n";
			}
		}

		void init_service(HttpServer& srv, Logger& logger)
		{
			log_ = &logger;

			log_->Debug("Init Device service");

			handlers.insert({ GetCapabilities, &GetCapabilitiesHandler });
			handlers.insert({ GetScopes, &GetScopesHandler });
			handlers.insert({ GetSystemDateAndTime, &GetSystemDateAndTimeHandler });

			srv.resource["/onvif/device_service"]["POST"] = DeviceServiceHandler;
		}

	}
}