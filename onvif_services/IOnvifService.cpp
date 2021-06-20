#include "IOnvifService.h"

#include "../Logger.h"

#include "../include/IOnvifServer.h"
#include "../Server.h"
#include "../Simple-Web-Server/server_http.hpp"

#include "onvif_services/service_configs.h"

#include "device_service.h"

#include "../utility/XmlParser.h"
#include "../utility/HttpHelper.h"

#include "../onvif/OnvifRequest.h"

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <exception>

namespace pt = boost::property_tree;

namespace osrv
{

	struct RequestHandler
	{
	public:
		RequestHandler(std::shared_ptr<IOnvifService> service, std::shared_ptr<ILogger> log)
			: service_(service)
			, log_(log)
		{}

		void operator()(std::shared_ptr<HttpServer::Response> response,
			std::shared_ptr<HttpServer::Request> request)
		{
			std::string method;
			pt::ptree* tree = new exns::Parser();
			try
			{
				pt::xml_parser::read_xml(std::istringstream(request->content.string()), *tree);
				auto* ptr = static_cast<exns::Parser*>(tree);
				method = static_cast<exns::Parser*>(tree)->___getMethod();
			}
			catch (const pt::xml_parser_error& e)
			{
				log_->Error(e.what());
			}

			
			auto handlers = service_->Handlers();
			auto handler_it = std::find_if(handlers.begin(), handlers.end(),
					[method](const HandlerSP handler)
					{
						return handler->name() == method;
					}
				);

			std::shared_ptr<ServerConfigs> srv_cfg = service_->OnvifServer()->ServerConfigs();
			if (handler_it != handlers.end())
			{
				//check user credentials
				try
				{
					auto handler_ptr = *handler_it;
					log_->Debug("Handling " + service_->ServiceName() + " request: " + handler_ptr->name());

					//extract user credentials
					osrv::auth::USER_TYPE current_user = osrv::auth::USER_TYPE::ANON;
					if (srv_cfg->auth_scheme_ == AUTH_SCHEME::DIGEST)
					{
						auto auth_header_it = request->header.find(utility::http::HEADER_AUTHORIZATION);
						if (auth_header_it != request->header.end())
						{
							//do extract user creds
							auto da_from_request = utility::digest::extract_DA(auth_header_it->second);

							bool isStaled;
							auto isCredsOk = srv_cfg->digest_session_->verifyDigest(da_from_request, isStaled);

							//if provided credentials are OK, upgrade UserType from Anon to appropriate Type
							if (isCredsOk)
							{
								current_user = osrv::auth::get_usertype_by_username(da_from_request.username,
									srv_cfg->digest_session_->get_users_list());
							}
						}

						if (!osrv::auth::isUserHasAccess(current_user, handler_ptr->security_level()))
						{
							throw osrv::auth::digest_failed{};
						}
					}

					(*handler_ptr)(response, request);
				}
				catch (const osrv::auth::digest_failed& e)
				{
					log_->Error(e.what());

					*response << utility::http::RESPONSE_UNAUTHORIZED << "\r\n"
						<< "Content-Type: application/soap+xml; charset=utf-8" << "\r\n"
						<< "Content-Length: " << 0 << "\r\n"
						<< utility::http::HEADER_WWW_AUTHORIZATION << ": " << srv_cfg->digest_session_->generateDigest().to_string() << "\r\n"
						<< "\r\n";
				}
				catch (const std::exception& e)
				{
					std::ostringstream oss;
					oss << "A server's error occured in " << service_->ServiceName() << " while processing: "
						<< method << ". Info: " << e.what();
					log_->Error(oss.str());

					*response << "HTTP/1.1 500 Server error\r\nContent-Length: " << 0 << "\r\n\r\n";
				}
			}
			else
			{
				std::ostringstream oss;
				oss << "Not found an appropriate handler in " << service_->ServiceName() << " for: " << method;
				log_->Error(oss.str());
				*response << "HTTP/1.1 400 Bad request\r\nContent-Length: " << 0 << "\r\n\r\n";
			}
		}

	private:
		std::shared_ptr<IOnvifService> service_;
		std::shared_ptr<ILogger> log_;
	};
}

osrv::IOnvifService::IOnvifService(
	const std::string& service_uri,
	const std::string& service_name,
	std::shared_ptr<IOnvifServer> srv)
		:
		service_uri_(service_uri)
		, service_name_(service_name)
		, configs_ptree_ {ServiceConfigs(service_name, srv->ConfigsPath())}
		, onvif_server_(srv)
		, http_server_(srv->HttpServer())
		, server_configs_(srv->ServerConfigs())
		, log_(srv->Logger())
{
}

void osrv::IOnvifService::Run()
{
	if (is_running_)
		return;

	// TODO: uncomment and use this after DeviceService implementation as OnvifService
	//const auto dvc_srv_cfg = onvif_server_->DeviceService()->configs_ptree_;;

	auto dvc_srv_cfg = device::get_configs_tree_instance();
	const auto service_config = dvc_srv_cfg.find("GetServices");

	auto namespaces_tree = configs_ptree_->get_child("Namespaces");
	for (const auto& n : namespaces_tree)
		xml_namespaces_.insert({ n.first, n.second.get_value<std::string>() });

	const auto uri = service_uri_;
	auto search_configs = std::find_if(service_config->second.begin(), service_config->second.end(),
		[uri](const pt::ptree::iterator::value_type tree) { return tree.second.get<std::string>("namespace") == uri; });

	std::string search_xaddr = "/" + search_configs->second.get<std::string>("XAddr");


	log_->Info("Running " + service_name_ + " service on address: " + search_xaddr);

	http_server_->resource[search_xaddr]["POST"] = [this](std::shared_ptr<HttpServer::Response> response,
		std::shared_ptr<HttpServer::Request> request) {
			RequestHandler(shared_from_this(), log_)(response, request);
	};

	is_running_ = true;
}
