#include "event_service.h"

#include "../Logger.h"
#include "../Server.h"
#include "../utility/HttpHelper.h"
#include "../utility/SoapHelper.h"
#include "../utility/XmlParser.h"
#include "device_service.h"
#include "pullpoint/pull_point.h"

#include "../utility/EventService.h"

#include "../Simple-Web-Server/server_http.hpp"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <map>
#include <vector>

using StringPairsList_t = std::vector<std::pair<std::string, std::string>>;

static ILogger* log_ = nullptr;

// this instance is required when handlers should be registered for a new PullPoint's subscriber
static osrv::HttpServer* http_server_intance = nullptr;

static const osrv::ServerConfigs* server_configs = nullptr;
static std::shared_ptr<utility::digest::IDigestSession> digest_session;

static std::unique_ptr<osrv::event::NotificationsManager> notifications_manager;

namespace pt = boost::property_tree;
static pt::ptree EVENT_CONFIGS_TREE;

static std::map<std::string, std::string> XML_NAMESPACES;

static std::string CONFIGS_PATH; // will be init with the service initialization
static const std::string EVENT_CONFIGS_FILE = "event.config";

// List of implemented methods of Events service port
const std::string GetEventProperties = "GetEventProperties";

namespace osrv
{
namespace event
{
static std::vector<utility::http::HandlerSP> handlers;

void do_handler_request(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);

// PullPoint handlers
struct CreatePullPointSubscriptionHandler : public utility::http::RequestHandlerBase
{
	CreatePullPointSubscriptionHandler()
			: utility::http::RequestHandlerBase("CreatePullPointSubscription", osrv::auth::SECURITY_LEVELS::READ_MEDIA)
	{
	}

	OVERLOAD_REQUEST_HANDLER
	{
		// TODO: Handler filters

		pt::ptree analytics_configs;
		auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);

		envelope_tree.add("s:Header.wsa:Action",
											"http://www.onvif.org/ver10/events/wsdl/EventPortType/CreatePullPointSubscriptionResponse");

		auto port = server_configs->http_port_;
		if (!EVENT_CONFIGS_TREE.get<bool>("PullPoint.UseHttpServerPort"))
			port = std::to_string(EVENT_CONFIGS_TREE.get<unsigned short>("PullPoint.Port"));
		std::string sub_ref = "http://";
		sub_ref += server_configs->ipv4_address_ + ":" + port + "/";

		auto pullpoint = notifications_manager->CreatePullPoint();
		sub_ref += pullpoint->GetSubscriptionReference();

		pt::ptree response_node;
		response_node.add("tet:SubscriptionReference.wsa:Address", sub_ref);

		response_node.add("wsnt:CurrentTime", pullpoint->GetLastRenew());
		response_node.add("wsnt:TerminationTime", pullpoint->GetTerminationTime());

		envelope_tree.add_child("s:Body.tet:CreatePullPointSubscriptionResponse", response_node);

		pt::ptree root_tree;
		root_tree.put_child("s:Envelope", envelope_tree);

		std::ostringstream os;
		pt::write_xml(os, root_tree);

		utility::http::fillResponseWithHeaders(*response, os.str());
	}
};

// PullPoint port entrance handler
void PullPointPortDefaultHandler(std::shared_ptr<HttpServer::Response> response,
																 std::shared_ptr<HttpServer::Request> request)
{
	// osrv::auth::SECURITY_LEVELS::READ_MEDIA
	auto request_tree = exns::to_ptree(request->content.string());

	auto header_action = exns::find_hierarchy("Envelope.Header.Action", request_tree);
	auto header_message_id = exns::find_hierarchy("Envelope.Header.MessageID", request_tree);
	auto header_to = exns::find_hierarchy("Envelope.Header.To", request_tree);

	log_->Debug("Handling PullPoint/" + header_action + ". Subscription: " + request->path);

	const static std::string ACTION_PULLMESSAGES =
			"http://www.onvif.org/ver10/events/wsdl/PullPointSubscription/PullMessagesRequest";
	const static std::string ACTION_RENEWREQUEST = "http://docs.oasis-open.org/wsn/bw-2/SubscriptionManager/RenewRequest";
	const static std::string ACTION_SETSYNCHRONIZATIONPOINT =
			"http://www.onvif.org/ver10/events/wsdl/PullPointSubscription/SetSynchronizationPointRequest";
	const static std::string ACTION_UNSUBSCRIBE =
			"http://docs.oasis-open.org/wsn/bw-2/SubscriptionManager/UnsubscribeRequest";

	if (header_action == ACTION_PULLMESSAGES)
	{
		auto timeout = exns::find_hierarchy("Envelope.Body.PullMessages.Timeout", request_tree);
		auto messages_limit = std::stoi((exns::find_hierarchy("Envelope.Body.PullMessages.MessageLimit", request_tree)));

		// NOTE: current implementation reads a timeout from the configuration and ignores a value in the request
		notifications_manager->PullMessages(response, header_to, header_message_id,
																				EVENT_CONFIGS_TREE.get<int>("PullPoint.Timeout"), messages_limit);

		// If there was no error, a response will be send asynchronously
	}
	else if (header_action == ACTION_RENEWREQUEST)
	{
		// it's not need now
		// auto termination_time = exns::find_hierarchy("Envelope.Body.PullMessages.TerminationTime", request_tree);
		notifications_manager->Renew(response, header_to, header_message_id);
	}
	else if (header_action == ACTION_SETSYNCHRONIZATIONPOINT)
	{
		try
		{
			notifications_manager->SetSynchronizationPoint(header_to);

			namespace pt = boost::property_tree;
			auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);
			envelope_tree.add("s:Header.wsa:MessageID", header_message_id);
			envelope_tree.add("s:Header.wsa:To", "http://www.w3.org/2005/08/addressing/anonymous");
			envelope_tree.add("s:Header.wsa:Action",
												"http://www.onvif.org/ver10/events/wsdl/PullPointSubscription/SetSynchronizationPointResponse");

			envelope_tree.add("s:Body.tet:SetSynchronizationPointResponse", "");

			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", envelope_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);

			utility::http::fillResponseWithHeaders(*response, os.str());
		}
		catch (const std::exception& e)
		{
			utility::http::fillResponseWithHeaders(*response, e.what(), utility::http::ClientErrorDefaultWriter);
		}
	}
	else if (header_action == ACTION_UNSUBSCRIBE)
	{
		notifications_manager->Unsubscribe(header_to);

		namespace pt = boost::property_tree;
		auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);
		envelope_tree.add("s:Header.wsa:MessageID", header_message_id);
		envelope_tree.add("s:Header.wsa:To", "http://www.w3.org/2005/08/addressing/anonymous");
		envelope_tree.add("s:Header.wsa:Action",
											"http://docs.oasis-open.org/wsn/bw-2/SubscriptionManager/UnsubscribeResponse");

		envelope_tree.add("s:Body.wsnt:UnsubscribeResponse", "");

		pt::ptree root_tree;
		root_tree.put_child("s:Envelope", envelope_tree);

		std::ostringstream os;
		pt::write_xml(os, root_tree);

		utility::http::fillResponseWithHeaders(*response, os.str());
	}
	else
	{
		// TODO: send something
		// *response << "HTTP/1.1 400 Bad request\r\n" << "Content-Length: 0\r\n" << "Connection: close\r\n" << "\r\n";
	}
}

// EVENTS SERVICE PORT
struct GetEventPropertiesHandler : public utility::http::RequestHandlerBase
{
	GetEventPropertiesHandler()
			: utility::http::RequestHandlerBase("GetEventProperties", osrv::auth::SECURITY_LEVELS::READ_MEDIA)
	{
	}

	OVERLOAD_REQUEST_HANDLER
	{
		auto configs_node = EVENT_CONFIGS_TREE.get_child(GetEventProperties);

		std::string response_body;
		auto isStaticResponse = configs_node.get<bool>("ReadResponseFromFile");
		if (isStaticResponse)
		{
			auto response_filename = configs_node.get<std::string>("ResponseFilePath");
			std::ifstream event_file(CONFIGS_PATH + response_filename);
			if (!event_file.is_open())
				throw std::runtime_error("Couldn't read specified response file: " + response_filename);

			std::string read_configs((std::istreambuf_iterator<char>(event_file)), (std::istreambuf_iterator<char>()));
			event_file.close();

			std::swap(response_body, read_configs);
		}
		else
		{
			auto request_tree = exns::to_ptree(request->content.string());

			namespace pt = boost::property_tree;
			auto envelope_tree = utility::soap::getEnvelopeTree(XML_NAMESPACES);
			envelope_tree.add("s:Header.wsa:To", "http://www.w3.org/2005/08/addressing/anonymous");
			envelope_tree.add("s:Header.wsa:Action",
												"http://www.onvif.org/ver10/events/wsdl/EventPortType/GetEventPropertiesResponse");

			pt::ptree response_tree;
			response_tree.add("tet:TopicNamespaceLocation", "http://www.onvif.org/onvif/ver10/topics/topicns.xml");
			response_tree.add("wsnt:FixedTopicSet", "true");

			{ // DI properties
				StringPairsList_t source_props = {{"InputToken", "tt:ReferenceToken"}};
				StringPairsList_t data_props = {{"LogicalState", "xsd:boolean"}};
				EventPropertiesSerializer serializer(EVENT_CONFIGS_TREE.get<std::string>("DigitalInputsAlarm.Topic"),
																						 source_props, data_props);

				response_tree.add_child("wstop:TopicSet." + serializer.Path(), serializer.Ptree());
				// TODO: check if this attr are required and move them into serializer impl
				// response_tree.add("wstop:TopicSet.tns1:Device.Trigger.<xmlattr>.wstop:topic", "true");
				// response_tree.add("wstop:TopicSet.tns1:Device.<xmlattr>.wstop:topic", "true");
			}

			{ // Motion alarm
				StringPairsList_t source_props = {{"Source", "tt:ReferenceToken"}};
				StringPairsList_t data_props = {{"State", "xsd:boolean"}};
				EventPropertiesSerializer serializer(EVENT_CONFIGS_TREE.get<std::string>("MotionAlarm.Topic"), source_props,
																						 data_props);

				response_tree.add_child("wstop:TopicSet." + serializer.Path(), serializer.Ptree());
			}

			{
				// Cell motion
				StringPairsList_t source_props;
				source_props.push_back(std::make_pair(
						EVENT_CONFIGS_TREE.get<std::string>("CellMotion.VideoSourceConfigurationToken"), "tt:ReferenceToken"));
				source_props.push_back(std::make_pair(
						EVENT_CONFIGS_TREE.get<std::string>("CellMotion.VideoAnalyticsConfigurationToken"), "tt:ReferenceToken"));
				source_props.push_back(std::make_pair(EVENT_CONFIGS_TREE.get<std::string>("CellMotion.Rule"), "xsd:string"));

				StringPairsList_t data_props;
				data_props.push_back(
						std::make_pair(EVENT_CONFIGS_TREE.get<std::string>("CellMotion.DataItemName"), "xsd:boolean"));

				EventPropertiesSerializer serializer(EVENT_CONFIGS_TREE.get<std::string>("CellMotion.Topic"), source_props,
																						 data_props);

				response_tree.add_child("wstop:TopicSet." + serializer.Path(), serializer.Ptree());
			}

			{
				// Audio detection
				StringPairsList_t source_props;
				source_props.push_back(std::make_pair(
						EVENT_CONFIGS_TREE.get<std::string>("AudioDetection.SourceConfigurationToken"), "tt:ReferenceToken"));
				source_props.push_back(std::make_pair(
						EVENT_CONFIGS_TREE.get<std::string>("AudioDetection.AnalyticsConfigurationToken"), "tt:ReferenceToken"));
				source_props.push_back(
						std::make_pair(EVENT_CONFIGS_TREE.get<std::string>("AudioDetection.Rule"), "xsd:string"));

				StringPairsList_t data_props;
				data_props.push_back(
						std::make_pair(EVENT_CONFIGS_TREE.get<std::string>("AudioDetection.DataItemName"), "xsd:boolean"));

				EventPropertiesSerializer serializer(EVENT_CONFIGS_TREE.get<std::string>("AudioDetection.Topic"), source_props,
																						 data_props);

				response_tree.add_child("wstop:TopicSet." + serializer.Path(), serializer.Ptree());
			}

			response_tree.add("wsnt:TopicExpressionDialect", "http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet");
			response_tree.add("wsnt:TopicExpressionDialect",
												"http://docs.oasis-open.org/wsnt/t-1/TopicExpression/ConcreteSet");
			response_tree.add("wsnt:TopicExpressionDialect", "http://docs.oasis-open.org/wsn/t-1/TopicExpression/Concrete");
			response_tree.add("tet:MessageContentFilterDialect",
												"http://www.onvif.org/ver10/tev/messageContentFilter/ItemFilter");
			response_tree.add("tet:MessageContentSchemaLocation", "http://www.onvif.org/onvif/ver10/schema/onvif.xsd");

			envelope_tree.add_child("s:Body.tet:GetEventPropertiesResponse", response_tree);

			pt::ptree root_tree;
			root_tree.put_child("s:Envelope", envelope_tree);

			std::ostringstream os;
			pt::write_xml(os, root_tree);
			response_body = os.str();
		}

		utility::http::fillResponseWithHeaders(*response, response_body);
	}
};

// DEFAULT HANDLER
void EventServiceHandler(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request)
{
	if (auto delay = server_configs->network_delay_simulation_; delay > 0)
	{
		auto timer = std::make_shared<boost::asio::deadline_timer>(*server_configs->io_context_,
																															 boost::posix_time::milliseconds(delay));
		timer->async_wait([timer, response, request](const boost::system::error_code& ec) {
			if (ec)
				return;

			do_handler_request(response, request);
		});
	}
	else
	{
		do_handler_request(response, request);
	}
}

void do_handler_request(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request)
{
	// extract requested method
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

	auto handler_it = std::find_if(handlers.begin(), handlers.end(), [&method](const utility::http::HandlerSP handler) {
		return handler->get_name() == method;
	});

	// handle requests
	if (handler_it != handlers.end())
	{
		// checking user credentials
		try
		{
			auto handler_ptr = *handler_it;
			log_->Debug("Handling EventService request: " + handler_ptr->get_name());

			// extract user credentials
			osrv::auth::USER_TYPE current_user = osrv::auth::USER_TYPE::ANON;
			if (server_configs->auth_scheme_ == osrv::AUTH_SCHEME::DIGEST)
			{
				auto auth_header_it = request->header.find(utility::http::HEADER_AUTHORIZATION);
				if (auth_header_it != request->header.end())
				{
					// do extract user creds
					auto da_from_request = utility::digest::extract_DA(auth_header_it->second);

					bool isStaled;
					auto isCredsOk = digest_session->verifyDigest(da_from_request, isStaled);

					// if provided credentials are OK, upgrade UserType from Anon to appropriate Type
					if (isCredsOk)
					{
						current_user =
								osrv::auth::get_usertype_by_username(da_from_request.username, digest_session->get_users_list());
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
			log_->Error(e.what());

			*response << utility::http::RESPONSE_UNAUTHORIZED << "\r\n"
								<< "Content-Type: application/soap+xml; charset=utf-8"
								<< "\r\n"
								<< "Content-Length: " << 0 << "\r\n"
								<< utility::http::HEADER_WWW_AUTHORIZATION << ": " << digest_session->generateDigest().to_string()
								<< "\r\n"
								<< "\r\n";
		}
		catch (const std::exception& e)
		{
			log_->Error("A server's error occured in DeviceService while processing: " + method + ". Info: " + e.what());

			*response << "HTTP/1.1 500 Server error\r\nContent-Length: " << 0 << "\r\n\r\n";
		}
	}
	else
	{
		log_->Error("Not found an appropriate handler in DeviceService for: " + method);
		*response << "HTTP/1.1 400 Bad request\r\nContent-Length: " << 0 << "\r\n\r\n";
	}
};

void init_service(HttpServer& srv, const osrv::ServerConfigs& server_configs_instance, const std::string& configs_path,
									ILogger& logger)
{
	if (log_ != nullptr)
		return log_->Error("EventService is already inited!");

	log_ = &logger;
	log_->Info("Initiating Event service...");

	std::unique_ptr<osrv::HttpServer> events_http_server;

	http_server_intance = &srv;

	server_configs = &server_configs_instance;
	digest_session = server_configs_instance.digest_session_;

	CONFIGS_PATH = configs_path;

	// getting service's configs
	pt::read_json(configs_path + EVENT_CONFIGS_FILE, EVENT_CONFIGS_TREE);

	auto namespaces_tree = EVENT_CONFIGS_TREE.get_child("Namespaces");
	for (const auto& n : namespaces_tree)
		XML_NAMESPACES.insert({n.first, n.second.get_value<std::string>()});

	notifications_manager =
			std::unique_ptr<osrv::event::NotificationsManager>(new osrv::event::NotificationsManager(logger, XML_NAMESPACES));

	// TODO: reading events generating interval from configs
	// add event generators
	auto di_event_generator = std::shared_ptr<osrv::event::DInputEventGenerator>(new event::DInputEventGenerator(
			EVENT_CONFIGS_TREE.get<int>("DigitalInputsAlarm.EventGenerationTimeout"),
			EVENT_CONFIGS_TREE.get<std::string>("DigitalInputsAlarm.Topic"), notifications_manager->GetIoContext(), *log_));
	di_event_generator->SetDigitalInputsList(server_configs->digital_inputs_);
	notifications_manager->AddGenerator(di_event_generator);

	// add motion alarms generator
	if (EVENT_CONFIGS_TREE.get<bool>("MotionAlarm.GenerateEvents"))
	{
		auto ma_event_generator = std::make_shared<osrv::event::MotionAlarmEventGenerator>(
				EVENT_CONFIGS_TREE.get<std::string>("MotionAlarm.Source"),
				EVENT_CONFIGS_TREE.get<int>("MotionAlarm.EventGenerationTimeout"),
				EVENT_CONFIGS_TREE.get<std::string>("MotionAlarm.Topic"), notifications_manager->GetIoContext(), *log_);

		notifications_manager->AddGenerator(ma_event_generator);
	}

	// add cell motion alarms generator
	if (EVENT_CONFIGS_TREE.get<bool>("CellMotion.GenerateEvents"))
	{
		auto cellmotion_generator = std::make_shared<osrv::event::CellMotionEventGenerator>(
				EVENT_CONFIGS_TREE.get<std::string>("CellMotion.VideoSourceConfigurationToken"),
				EVENT_CONFIGS_TREE.get<std::string>("CellMotion.VideoAnalyticsConfigurationToken"),
				EVENT_CONFIGS_TREE.get<std::string>("CellMotion.Rule"),
				EVENT_CONFIGS_TREE.get<std::string>("CellMotion.DataItemName"),
				EVENT_CONFIGS_TREE.get<int>("CellMotion.EventGenerationTimeout"),
				EVENT_CONFIGS_TREE.get<std::string>("CellMotion.Topic"), notifications_manager->GetIoContext(), *log_);

		notifications_manager->AddGenerator(cellmotion_generator);
	}

	// add audio detection alarms generator
	if (EVENT_CONFIGS_TREE.get<bool>("AudioDetection.GenerateEvents"))
	{
		auto audio_generator = std::make_shared<osrv::event::AudioDetectectionEventGenerator>(
				EVENT_CONFIGS_TREE.get<std::string>("AudioDetection.SourceConfigurationToken"),
				EVENT_CONFIGS_TREE.get<std::string>("AudioDetection.AnalyticsConfigurationToken"),
				EVENT_CONFIGS_TREE.get<std::string>("AudioDetection.Rule"),
				EVENT_CONFIGS_TREE.get<std::string>("AudioDetection.DataItemName"),
				EVENT_CONFIGS_TREE.get<int>("AudioDetection.EventGenerationTimeout"),
				EVENT_CONFIGS_TREE.get<std::string>("AudioDetection.Topic"), notifications_manager->GetIoContext(), *log_);

		notifications_manager->AddGenerator(audio_generator);
	}

	notifications_manager->Run();

	// event service handlers
	handlers.emplace_back(new GetEventPropertiesHandler{});

	// PullPoint handlers
	handlers.emplace_back(new CreatePullPointSubscriptionHandler{});

	srv.resource["/onvif/event_service"]["POST"] = EventServiceHandler;

	// use this pattern to register a default handler for the Pullpoint requests
	// NOTE: this path pattern should be match the one generated
	// in the NotificationsManager for a new subscription
	const std::string SUBSCRIPTIONS_REFERENCES = "/onvif/event_service/s([0-9]+)";

	bool useOwnPort = !EVENT_CONFIGS_TREE.get<bool>("PullPoint.UseHttpServerPort");
	if (useOwnPort)
	{
		auto ownPort = EVENT_CONFIGS_TREE.get<unsigned short>("PullPoint.Port");
		events_http_server.reset(new osrv::HttpServer());
		events_http_server->config.address = server_configs_instance.ipv4_address_;
		events_http_server->config.port = ownPort;
		events_http_server->resource[SUBSCRIPTIONS_REFERENCES]["POST"] = PullPointPortDefaultHandler;

		// TODO: join this
		std::thread t([server = std::move(events_http_server), l = log_]() {
			try
			{
				server->start();
			}
			catch (const std::exception& e)
			{
				l->Error(std::string("Events http server finished with error: ") + e.what());
			}
		});

		t.detach();
	}
	else
	{

		srv.resource[SUBSCRIPTIONS_REFERENCES]["POST"] = PullPointPortDefaultHandler;
	}

} // init service

} // namespace event
} // namespace osrv