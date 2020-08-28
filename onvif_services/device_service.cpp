#include "device_service.h"

#include "../Logger.hpp"
#include "../utility/XmlParser.h"
#include "../utility/HttpResponseHelper.h"

#include "../Simple-Web-Server/server_http.hpp"

#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <map>

static Logger* log_ = nullptr;

//List of implemented methods
const std::string GetCapabilities = "GetCapabilities";
const std::string GetScopes = "GetScopes";
const std::string GetSystemDateAndTime = "GetSystemDateAndTime";

namespace pt = boost::property_tree;
static pt::ptree CONFIGS_TREE;
static std::vector<std::pair<std::string, std::string> > XML_NAMESPACES;

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

			pt::ptree envelope_tree;
			envelope_tree.put("s:Header", "");
			
			for (auto it : XML_NAMESPACES)
			{
				envelope_tree.put("<xmlattr>.xmlns:" + it.first,
					it.second);
			}

			static const auto SCOPES_TREE = CONFIGS_TREE.get_child("GetScopes");
			for (const auto& it : SCOPES_TREE)
			{
				pt::ptree scopes_tree;
				scopes_tree.put("tt:ScopeDef", "Fixed");
				scopes_tree.put("tt:ScopeItem",
					"onvif://www.onvif.org/" + it.first + "/" + it.second.get_value<std::string>());

				envelope_tree.add_child("s:Body.tds:GetScopesResponse", scopes_tree);
			}

			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", envelope_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);

			utility::http::addHeaders(*response, os.str());
		}

		void GetSystemDateAndTimeHandler(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request)
		{
			log_->Debug("Handle GetSystemDateAndTime");

			pt::ptree envelope_tree;
			envelope_tree.put("s:Header", "");

			for (auto it : XML_NAMESPACES)
			{
				envelope_tree.put("<xmlattr>.xmlns:" + it.first,
					it.second);
			}
			
			envelope_tree.put("s:Body.tds:GetSystemDateAndTimeResponse.tds:SystemDateAndTime.tt:DateTimeType", "NTP");
			envelope_tree.put("s:Body.tds:GetSystemDateAndTimeResponse.tds:SystemDateAndTime.tt:DaylightSavings", "false");

			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", envelope_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);

			utility::http::addHeaders(*response, os.str());
		}
		
		//DEFAULT HANDLER
		void DeviceServiceHandler(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request)
		{
			//extract requested method
			std::string method;
			auto content = request->content.string();
			std::istringstream is(content);
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

			//handle requests
			if (it != handlers.end())
			{
				try
				{
					it->second(response, request);
				}
				catch (const std::exception& e)
				{
					log_->Error("A server's error occured while processing: " + method
						+ ". Info: " + e.what());
				
					*response << "HTTP/1.1 500 Server error\r\nContent-Length: " << 0 << "\r\n\r\n";
				}
			}
			else
			{
				log_->Debug("Not found an appropriate handler for: " + method);
				*response << "HTTP/1.1 400 Bad request\r\nContent-Length: " << 0 << "\r\n\r\n";
			}
		}

		void init_service(HttpServer& srv, const std::string& configs_path, Logger& logger)
		{
			if (log_ != nullptr)
				return log_->Error("Device service is already inited!");

			log_ = &logger;

			log_->Debug("Initiating Device service");

			//getting service's configs
			pt::read_json(configs_path, CONFIGS_TREE);
			
			auto namespaces_tree = CONFIGS_TREE.get_child("Namespaces");
			for (const auto& n : namespaces_tree)
				XML_NAMESPACES.push_back({ n.first, n.second.get_value<std::string>() });

			handlers.insert({ GetCapabilities, &GetCapabilitiesHandler });
			handlers.insert({ GetScopes, &GetScopesHandler });
			handlers.insert({ GetSystemDateAndTime, &GetSystemDateAndTimeHandler });

			srv.resource["/onvif/device_service"]["POST"] = DeviceServiceHandler;
		}

	}
}