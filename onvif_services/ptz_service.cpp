#include "ptz_service.h"

#include "../Logger.h"
#include "../Server.h"

#include "../utility/HttpHelper.h"
#include "../utility/SoapHelper.h"
#include "../utility/XmlParser.h"

#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

static ILogger* logger_ = nullptr;
static osrv::ServerConfigs* server_configs;
static DigestSessionSP digest_session;

static std::string CONFIGS_PATH; //will be init with the service initialization

namespace pt = boost::property_tree;
static pt::ptree CONFIGS_TREE;
static osrv::StringsMap XML_NAMESPACES;

// a list of implemented methods
const std::string GetCompatibleConfigurations = "GetCompatibleConfigurations";
const std::string GetConfiguration = "GetConfiguration";
const std::string GetConfigurations = "GetConfigurations";
const std::string GetNodes = "GetNodes";
const std::string GetNode = "GetNode";
const std::string SetConfiguration = "SetConfiguration";


static std::vector<utility::http::HandlerSP> handlers;

namespace osrv::ptz
{
	const std::string CONFIGS_FILE = "ptz.config";

	void do_handle_request(std::shared_ptr<HttpServer::Response> response,
		std::shared_ptr<HttpServer::Request> request);

	struct GetCompatibleConfigurationsHandler : public utility::http::RequestHandlerBase
	{

		GetCompatibleConfigurationsHandler() : utility::http::RequestHandlerBase(GetCompatibleConfigurations, osrv::auth::SECURITY_LEVELS::READ_MEDIA)
		{
		}

		OVERLOAD_REQUEST_HANDLER
		{
			auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);

			// TODO: impl. reading configs from a file
			pt::ptree response_node;

			{
				pt::ptree ptz_node;
				ptz_node.add("<xmlattr>.token", "PtzConfigToken0");
				ptz_node.add("tt:Name", "PtzConfig0");
				ptz_node.add("tt:UseCount", 3);
				ptz_node.add("tt:NodeToken", "PTZNODE_1");
				ptz_node.add("tt:DefaultContinuousPanTiltVelocitySpace",
					"http://www.onvif.org/ver10/tptz/PanTiltSpaces/VelocityGenericSpace");
				ptz_node.add("tt:DefaultContinuousZoomVelocitySpace",
					"http://www.onvif.org/ver10/tptz/ZoomSpaces/VelocityGenericSpace");

				response_node.add_child("PTZConfiguration", ptz_node);
			}

			envelope_tree.add_child("s:Body.tptz:GetCompatibleConfigurationsResponse", response_node);

			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", envelope_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);

			utility::http::fillResponseWithHeaders(*response, os.str());
		}
	};

	struct GetConfigurationHandler : public utility::http::RequestHandlerBase
	{

		GetConfigurationHandler() : utility::http::RequestHandlerBase(GetConfiguration, osrv::auth::SECURITY_LEVELS::READ_MEDIA)
		{
		}

		OVERLOAD_REQUEST_HANDLER
		{
			auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);

			// TODO: impl. reading configs from a file
			pt::ptree response_node;

			{
				pt::ptree ptz_node;
				ptz_node.add("<xmlattr>.token", "PtzConfigToken0");
				ptz_node.add("tt:Name", "PtzConfig0");
				ptz_node.add("tt:UseCount", 3);
				ptz_node.add("tt:NodeToken", "PTZNODE_1");
				ptz_node.add("tt:DefaultContinuousPanTiltVelocitySpace",
					"http://www.onvif.org/ver10/tptz/PanTiltSpaces/VelocityGenericSpace");
				ptz_node.add("tt:DefaultContinuousZoomVelocitySpace",
					"http://www.onvif.org/ver10/tptz/ZoomSpaces/VelocityGenericSpace");

				response_node.add_child("PTZConfiguration", ptz_node);
			}

			envelope_tree.add_child("s:Body.tptz:GetConfigurationResponse", response_node);

			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", envelope_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);

			utility::http::fillResponseWithHeaders(*response, os.str());
		}
	};

	struct GetConfigurationsHandler : public utility::http::RequestHandlerBase
	{

		GetConfigurationsHandler() : utility::http::RequestHandlerBase(GetConfigurations, osrv::auth::SECURITY_LEVELS::READ_MEDIA)
		{
		}

		OVERLOAD_REQUEST_HANDLER
		{
			auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);

			// TODO: impl. reading configs from a file
			pt::ptree response_node;

			{
				pt::ptree ptz_node;
				ptz_node.add("<xmlattr>.token", "PtzConfigToken0");
				ptz_node.add("tt:Name", "PtzConfig0");
				ptz_node.add("tt:UseCount", 3);
				ptz_node.add("tt:NodeToken", "PTZNODE_1");
				ptz_node.add("tt:DefaultContinuousPanTiltVelocitySpace",
					"http://www.onvif.org/ver10/tptz/PanTiltSpaces/VelocityGenericSpace");
				ptz_node.add("tt:DefaultContinuousZoomVelocitySpace",
					"http://www.onvif.org/ver10/tptz/ZoomSpaces/VelocityGenericSpace");

				response_node.add_child("PTZConfiguration", ptz_node);
			}

			envelope_tree.add_child("s:Body.tptz:GetConfigurationsResponse", response_node);

			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", envelope_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);

			utility::http::fillResponseWithHeaders(*response, os.str());
		}
	};

	struct GetNodesHandler : public utility::http::RequestHandlerBase
	{

		GetNodesHandler() : utility::http::RequestHandlerBase(GetNodes, osrv::auth::SECURITY_LEVELS::READ_MEDIA)
		{
		}

		OVERLOAD_REQUEST_HANDLER
		{
			auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);

			pt::ptree nodes_tree;

			auto nodes_config = CONFIGS_TREE.get_child("Nodes");

			for (const auto& node : nodes_config)
			{
				pt::ptree node_tree;
				node_tree.add("<xmlattr>.token", node.second.get<std::string>("token"));
				node_tree.add("<xmlattr>.FixedHomePosition", node.second.get<bool>("FixedHomePosition"));
				node_tree.add("<xmlattr>.GeoMove", node.second.get<bool>("GeoMove"));
				node_tree.add("tt:Name", node.second.get<std::string>("Name"));

				for (const auto& space_node : node.second.get_child("SupportedPTZSpaces"))
				{
					const auto& space_name = space_node.second.get<std::string>("space");
					const auto item_path = "tt:SupportedPTZSpaces." + space_name;
					node_tree.add(item_path + "URI", space_node.second.get<std::string>("URI"));
					node_tree.add(item_path + "XRange.Min", space_node.second.get<std::string>("XRange.Min"));
					node_tree.add(item_path + "XRange.Max", space_node.second.get<std::string>("XRange.Max"));
					node_tree.add(item_path + "YRange.Min", space_node.second.get<std::string>("YRange.Min"));
					node_tree.add(item_path + "YRange.Max", space_node.second.get<std::string>("YRange.Max"));
				}

				node_tree.add("tt:MaximumNumberOfPresets", node.second.get<int>("MaximumNumberOfPresets"));
				node_tree.add("tt:HomeSupported", node.second.get<bool>("HomeSupported"));

				nodes_tree.add_child("tptz:PTZNode", node_tree);
			}

			envelope_tree.add_child("s:Body.tptz:GetNodesResponse", nodes_tree);

			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", envelope_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);

			utility::http::fillResponseWithHeaders(*response, os.str());
		}
	};
	
	struct GetNodeHandler : public utility::http::RequestHandlerBase
	{

		GetNodeHandler() : utility::http::RequestHandlerBase(GetNode, osrv::auth::SECURITY_LEVELS::READ_MEDIA)
		{
		}

		OVERLOAD_REQUEST_HANDLER
		{
			auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);

			pt::ptree nodes_tree;

			auto nodes_config = CONFIGS_TREE.get_child("Nodes");

			// TODO: read only configs for required token
			for (const auto& node : nodes_config)
			{
				pt::ptree node_tree;
				node_tree.add("<xmlattr>.token", node.second.get<std::string>("token"));
				node_tree.add("<xmlattr>.FixedHomePosition", node.second.get<bool>("FixedHomePosition"));
				node_tree.add("<xmlattr>.GeoMove", node.second.get<bool>("GeoMove"));
				node_tree.add("tt:Name", node.second.get<std::string>("Name"));

				for (const auto& space_node : node.second.get_child("SupportedPTZSpaces"))
				{
					const auto& space_name = space_node.second.get<std::string>("space");
					const auto item_path = "tt:SupportedPTZSpaces." + space_name;
					node_tree.add(item_path + "URI", space_node.second.get<std::string>("URI"));
					node_tree.add(item_path + "XRange.Min", space_node.second.get<std::string>("XRange.Min"));
					node_tree.add(item_path + "XRange.Max", space_node.second.get<std::string>("XRange.Max"));
					node_tree.add(item_path + "YRange.Min", space_node.second.get<std::string>("YRange.Min"));
					node_tree.add(item_path + "YRange.Max", space_node.second.get<std::string>("YRange.Max"));
				}

				node_tree.add("tt:MaximumNumberOfPresets", node.second.get<int>("MaximumNumberOfPresets"));
				node_tree.add("tt:HomeSupported", node.second.get<bool>("HomeSupported"));

				nodes_tree.add_child("tptz:PTZNode", node_tree);
			}

			envelope_tree.add_child("s:Body.tptz:GetNodesResponse", nodes_tree);

			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", envelope_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);

			utility::http::fillResponseWithHeaders(*response, os.str());
		}
	};
	
	struct SetConfigurationHandler : public utility::http::RequestHandlerBase
	{

		SetConfigurationHandler() : utility::http::RequestHandlerBase(SetConfiguration, osrv::auth::SECURITY_LEVELS::ACTUATE)
		{
		}

		OVERLOAD_REQUEST_HANDLER
		{
			auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);

			envelope_tree.add("s:Body.tptz:SetConfigurationResponse", "");

			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", envelope_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);

			utility::http::fillResponseWithHeaders(*response, os.str());
		}
	};


	void PtzServiceDefaultHandler(std::shared_ptr<HttpServer::Response> response,
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
				logger_->Debug("Handling PtzService request: " + handler_ptr->get_name());

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
				logger_->Error("A server's error occured in PtzService while processing: " + method
					+ ". Info: " + e.what());

				*response << "HTTP/1.1 500 Server error\r\nContent-Length: " << 0 << "\r\n\r\n";
			}
		}
		else
		{
			logger_->Error("Not found an appropriate handler in PtzService for: " + method);
			*response << "HTTP/1.1 400 Bad request\r\nContent-Length: " << 0 << "\r\n\r\n";
		}
	}

	void init_service(HttpServer& srv, osrv::ServerConfigs& server_configs_instance, const std::string& configs_path, ILogger& logger)
	{
		if (logger_)
			return logger_->Error("PtzService is already initiated!");

		logger_ = &logger;
		logger_->Debug("Initiating Ptz service...");

		server_configs = &server_configs_instance;
		digest_session = server_configs_instance.digest_session_;

		CONFIGS_PATH = configs_path;

		//getting service's configs
		pt::read_json(configs_path + CONFIGS_FILE, CONFIGS_TREE);

		auto namespaces_tree = CONFIGS_TREE.get_child("Namespaces");
		for (const auto& n : namespaces_tree)
			XML_NAMESPACES.insert({ n.first, n.second.get_value<std::string>() });

		handlers.emplace_back(new GetCompatibleConfigurationsHandler());
		handlers.emplace_back(new GetConfigurationHandler());
		handlers.emplace_back(new GetConfigurationsHandler());
		handlers.emplace_back(new GetNodeHandler());
		handlers.emplace_back(new GetNodesHandler());
		handlers.emplace_back(new SetConfigurationHandler());

		srv.resource["/onvif/ptz_service"]["POST"] = PtzServiceDefaultHandler;
	}
} // ptz