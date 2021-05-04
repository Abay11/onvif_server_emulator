#include "recording_search_service.h"

#include "../Logger.h"

#include "../Server.h"


//#include "device_service.h"

//#include "../Simple-Web-Server/server_http.hpp"

//static ILogger* log_ = nullptr;
//static osrv::HttpServer* http_server_intance = nullptr;
//static const osrv::ServerConfigs* server_configs = nullptr;
//static std::shared_ptr<utility::digest::IDigestSession> digest_session;

namespace osrv
{

	//void request_handler(std::shared_ptr<HttpServer::Response> response,
	//	std::shared_ptr<HttpServer::Request> request)
	//{
	//	//extract requested method
	//	std::string method;
	//	auto content = request->content.string();
	//	std::istringstream is(content);
	///*	pt::ptree* tree = new exns::Parser();
	//	try
	//	{
	//		pt::xml_parser::read_xml(is, *tree);
	//		auto* ptr = static_cast<exns::Parser*>(tree);
	//		method = static_cast<exns::Parser*>(tree)->___getMethod();
	//	}
	//	catch (const pt::xml_parser_error& e)
	//	{
	//		log_->Error(e.what());
	//	}

	//	auto handler_it = std::find_if(handlers.begin(), handlers.end(),
	//		[&method](const utility::http::HandlerSP handler) {
	//			return handler->get_name() == method;
	//		});*/

	//	//handle requests
	//	//if (handler_it != handlers.end())
	//	//{
	//	//	//checking user credentials
	//	//	try
	//	//	{
	//	//		auto handler_ptr = *handler_it;
	//	//		log_->Debug("Handling EventService request: " + handler_ptr->get_name());

	//	//		//extract user credentials
	//	//		osrv::auth::USER_TYPE current_user = osrv::auth::USER_TYPE::ANON;
	//	//		if (server_configs->auth_scheme_ == osrv::AUTH_SCHEME::DIGEST)
	//	//		{
	//	//			auto auth_header_it = request->header.find(utility::http::HEADER_AUTHORIZATION);
	//	//			if (auth_header_it != request->header.end())
	//	//			{
	//	//				//do extract user creds
	//	//				auto da_from_request = utility::digest::extract_DA(auth_header_it->second);

	//	//				bool isStaled;
	//	//				//auto isCredsOk = digest_session->verifyDigest(da_from_request, isStaled);

	//	//				//if provided credentials are OK, upgrade UserType from Anon to appropriate Type
	//	//				if (isCredsOk)
	//	//				{
	//	//					current_user = osrv::auth::get_usertype_by_username(da_from_request.username, digest_session->get_users_list());
	//	//				}
	//	//			}

	//	//			if (!osrv::auth::isUserHasAccess(current_user, handler_ptr->get_security_level()))
	//	//			{
	//	//				throw osrv::auth::digest_failed{};
	//	//			}
	//	//		}

	//	//		(*handler_ptr)(response, request);
	//	//	}
	//	//	catch (const osrv::auth::digest_failed& e)
	//	//	{
	//	//		log_->Error(e.what());

	//	//		*response << utility::http::RESPONSE_UNAUTHORIZED << "\r\n"
	//	//			<< "Content-Type: application/soap+xml; charset=utf-8" << "\r\n"
	//	//			<< "Content-Length: " << 0 << "\r\n"
	//	//			<< utility::http::HEADER_WWW_AUTHORIZATION << ": " << digest_session->generateDigest().to_string() << "\r\n"
	//	//			<< "\r\n";
	//	//	}
	//	//	catch (const std::exception& e)
	//	//	{
	//	//		log_->Error("A server's error occured in DeviceService while processing: " + method
	//	//			+ ". Info: " + e.what());

	//	//		*response << "HTTP/1.1 500 Server error\r\nContent-Length: " << 0 << "\r\n\r\n";
	//	//	}
	//	//}
	//	//else
	//	{
	//		log_->Error("Not found an appropriate handler in Search service for: " + method);
	//		*response << "HTTP/1.1 400 Bad request\r\nContent-Length: " << 0 << "\r\n\r\n";
	//	}
	//};

	//void SearchServiceHandler(std::shared_ptr<HttpServer::Response> response,
	//	std::shared_ptr<HttpServer::Request> request)
	//{
	//	//if (auto delay = server_configs->network_delay_simulation_; delay > 0)
	//	{
	//		//auto timer = std::make_shared<boost::asio::deadline_timer>(*server_configs->io_context_,
	//		//boost::posix_time::milliseconds(delay));
	//		//timer->async_wait(
	//		//	[timer, response, request](const boost::system::error_code& ec)
	//		//	{
	//		//		if (ec)
	//		//			return;

	//		//		//do_handler_request(response, request);
	//		//	}
	//		//);
	//	}
	//	//else
	//	{
	//		request_handler(response, request);
	//	}
	//}

	namespace pt = boost::property_tree;

	//void /*recording_search::*/init_service(HttpServer& srv, const osrv::ServerConfigs& server_configs_instance, const std::string&, ILogger& logger)
	//{
	//	if (log_ != nullptr)
	//		return log_->Error("Recording search service is already initiated!");

	//	log_ = &logger;
	//	log_->Info("Initiating Recording search service...");

	//	http_server_intance = &srv;

	//	server_configs = &server_configs_instance;
	//	//digest_session = server_configs_instance.digest_session_;

	//	auto device_service_configs = device::get_configs_tree_instance();
	//	auto service_config = device_service_configs.find("GetServices");
	//	if (service_config == device_service_configs.not_found())
	//		throw std::runtime_error("Not found GetService configs");

	//	auto search_configs = std::find_if(service_config->second.begin(), service_config->second.end(),
	//		[](const pt::ptree::iterator::value_type tree) { return tree.second.get<std::string>("namespace") == NS; });

	//	if (search_configs == service_config->second.end())
	//		throw std::runtime_error("Not found Replay service information");

	//	std::string search_xaddr = "/" + search_configs->second.get<std::string>("XAddr");

	//	srv.resource[search_xaddr]["POST"] = SearchServiceHandler;

	//	//CONFIGS_PATH = configs_path;
	//}

	RecordingSearchService::RecordingSearchService(const std::string& service_uri,
		const std::string& service_name, std::shared_ptr<IOnvifServer> srv)
		: IOnvifService(service_uri, service_name, srv)
	{
	}
	
	void RecordingSearchService::handleRequestImpl(std::shared_ptr<std::ostream> response,
		const std::istream& request)
	{
	}
}