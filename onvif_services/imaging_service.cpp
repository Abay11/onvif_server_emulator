#include "imaging_service.h"

#include "../Logger.h"
#include "../Server.h"

#include "../utility/HttpHelper.h"
#include "../utility/SoapHelper.h"
#include "../utility/XmlParser.h"

#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

//#include "../utility/HttpDigestHelper.h"

static ILogger* logger_ = nullptr;
static osrv::ServerConfigs* server_configs;
static DigestSessionSP digest_session;

static std::string CONFIGS_PATH; //will be init with the service initialization

namespace pt = boost::property_tree;
static pt::ptree CONFIGS_TREE;
static osrv::StringsMap XML_NAMESPACES;

namespace osrv::imaging
{
	// a list of implemented methods
	const std::string GetOptions = "GetOptions";
		
	const std::string CONFIGS_FILE = "imaging.config";

	static std::vector<utility::http::HandlerSP> handlers;

	void do_handle_request(std::shared_ptr<HttpServer::Response> response,
		std::shared_ptr<HttpServer::Request> request);

	struct GetOptionsHandler : public utility::http::RequestHandlerBase
	{

		GetOptionsHandler() : utility::http::RequestHandlerBase("GetOptions", osrv::auth::SECURITY_LEVELS::READ_MEDIA)
		{
		}
			
		OVERLOAD_REQUEST_HANDLER
		{
			auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);

			envelope_tree.add("s:Body.timg:GetOptionsResponse.timg:ImagingOptions", "");

			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", envelope_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);

			utility::http::fillResponseWithHeaders(*response, os.str());
		}
	};

	void ImagingServiceDefaultHandler(std::shared_ptr<HttpServer::Response> response,
		std::shared_ptr<HttpServer::Request> request)
	{
		if (auto delay = server_configs->network_delay_simulation_; delay > 0)
		{
			auto timer = std::make_shared<boost::asio::deadline_timer>(*server_configs->io_context_,
				boost::posix_time::milliseconds(delay));
			timer->async_wait(
				[timer, response, request](const boost::system::error_code& ec)
				{
					if (ec)
						return;

					do_handle_request(response, request);
				}
			);

		}
		else
		{
			do_handle_request(response, request);
		}
	}

	void do_handle_request(std::shared_ptr<HttpServer::Response> response,
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
			logger_->Error(e.what());
		}

		auto handler_it = std::find_if(handlers.begin(), handlers.end(),
			[&method](const utility::http::HandlerSP handler)
			{
				return handler->get_name() == method;
			}
		);

		//handle requests
		if (handler_it != handlers.end())
		{
			//TODO: Refactor and take out to general place this authentication logic
			//check user credentials
			try
			{
				auto handler_ptr = *handler_it;
				logger_->Debug("Handling ImagingService request: " + handler_ptr->get_name());

				//extract user credentials
				osrv::auth::USER_TYPE current_user = osrv::auth::USER_TYPE::ANON;
				if (server_configs->auth_scheme_ == AUTH_SCHEME::DIGEST)
				{
					auto auth_header_it = request->header.find(utility::http::HEADER_AUTHORIZATION);
					if (auth_header_it != request->header.end())
					{
						//do extract user creds
						auto da_from_request = utility::digest::extract_DA(auth_header_it->second);

						bool isStaled;
						auto isCredsOk = digest_session->verifyDigest(da_from_request, isStaled);

						//if provided credentials are OK, upgrade UserType from Anon to appropriate Type
						if (isCredsOk)
						{
							current_user = osrv::auth::get_usertype_by_username(da_from_request.username, digest_session->get_users_list());
						}
					}

					if (!osrv::auth::isUserHasAccess(current_user, handler_ptr->get_security_level()))
					{
						throw osrv::auth::digest_failed{};
					}
				}

				(*handler_ptr)(response, request);
			}
			catch (const osrv::auth::digest_failed& e)
			{
				logger_->Error(e.what());

				*response << utility::http::RESPONSE_UNAUTHORIZED << "\r\n"
					<< "Content-Type: application/soap+xml; charset=utf-8" << "\r\n"
					<< "Content-Length: " << 0 << "\r\n"
					<< utility::http::HEADER_WWW_AUTHORIZATION << ": " << digest_session->generateDigest().to_string() << "\r\n"
					<< "\r\n";
			}
			catch (const std::exception& e)
			{
				logger_->Error("A server's error occured in DeviceService while processing: " + method
					+ ". Info: " + e.what());

				*response << "HTTP/1.1 500 Server error\r\nContent-Length: " << 0 << "\r\n\r\n";
			}
		}
		else
		{
			logger_->Error("Not found an appropriate handler in DeviceService for: " + method);
			*response << "HTTP/1.1 400 Bad request\r\nContent-Length: " << 0 << "\r\n\r\n";
		}
	}

	void init_service(HttpServer& srv, osrv::ServerConfigs& server_configs_instance, const std::string& configs_path, ILogger& logger)
	{
		if (logger_)
			return logger_->Error("ImagingService is already initiated!");

			logger_ = &logger;
			logger_->Debug("Initiating Imaging service...");

			server_configs = &server_configs_instance;
			digest_session = server_configs_instance.digest_session_;

			CONFIGS_PATH = configs_path;

			//getting service's configs
			pt::read_json(configs_path + CONFIGS_FILE, CONFIGS_TREE);

			auto namespaces_tree = CONFIGS_TREE.get_child("Namespaces");
			for (const auto& n : namespaces_tree)
				XML_NAMESPACES.insert({ n.first, n.second.get_value<std::string>() });
			
			handlers.emplace_back(new GetOptionsHandler());

			srv.resource["/onvif/imaging_service"]["POST"] = ImagingServiceDefaultHandler;
	}
}