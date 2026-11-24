#include "../Server.h"
#include "../Simple-Web-Server/client_http.hpp"
#include "../include/LoggerFactories.h"
#include "../onvif/OnvifRequest.h"
#include "../onvif_services/IOnvifService.h"
#include <atomic>
#include <future>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <thread>

#include <boost/test/unit_test.hpp>

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <fstream>

namespace pt = boost::property_tree;

BOOST_AUTO_TEST_CASE(device_service_configs_test)
{
	// check real config file
	std::fstream config_file("../../server_configs/device.config", std::ios_base::in);
	if (!config_file.is_open())
		BOOST_TEST_FAIL("Could not open device config");

	pt::ptree configs;
	pt::read_json(config_file, configs);

	BOOST_CHECK_MESSAGE(!configs.empty(), "Could not open device config");

	auto getservices_configs = configs.find("GetServices");
	BOOST_CHECK_MESSAGE(getservices_configs != configs.not_found(), "Not found GetService configs");

	// BOOST_TEST();
}

namespace
{
struct DeviceTestServer : osrv::IOnvifServer
{
	DeviceTestServer(const std::string& path, std::function<void()> reboot)
			: IOnvifServer(path, std::shared_ptr<ILogger>(ConsoleLoggerFactory().GetLogger(ILogger::LVL_ERR)))
	{
		server_configs_ = osrv::read_server_configs(path + "/common.config");
		server_configs_->reboot = std::move(reboot);
	}
};
struct DeviceEndpoint
{
	const std::string path = "../../server_configs";
	std::shared_ptr<DeviceTestServer> server;
	std::thread worker;
	std::unique_ptr<SimpleWeb::Client<SimpleWeb::HTTP>> client;
	DeviceEndpoint(std::function<void()> reboot = {}, osrv::AUTH_SCHEME scheme = osrv::AUTH_SCHEME::NONE)
			: server(std::make_shared<DeviceTestServer>(path, reboot))
	{
		server->GetServerConfigs()->auth_scheme_ = scheme;
		server->GetServerConfigs()->digest_session_ = std::make_shared<utility::digest::DigestSessionImpl>();
		server->GetServerConfigs()->digest_session_->set_users_list(server->GetServerConfigs()->system_users_);
		server->DeviceService()->Run();
		auto http = server->GetHttpServer();
		http->config.address = "127.0.0.1";
		http->config.port = 0;
		std::promise<unsigned short> port;
		auto ready = port.get_future();
		worker = std::thread([http, &port] { http->start([&port](unsigned short value) { port.set_value(value); }); });
		client = std::make_unique<SimpleWeb::Client<SimpleWeb::HTTP>>("127.0.0.1:" + std::to_string(ready.get()));
		client->config.timeout = 5;
	}
	~DeviceEndpoint()
	{
		server->GetHttpServer()->stop();
		worker.join();
	}
	auto Call(const std::string& operation = "<d:SystemReboot/>",
						const std::string& ns = "http://www.onvif.org/ver10/device/wsdl")
	{
		return client->request("POST", "/onvif/device_service",
													 "<s:Envelope xmlns:s='http://www.w3.org/2003/05/soap-envelope' xmlns:d='" + ns +
															 "'><s:Body>" + operation + "</s:Body></s:Envelope>",
													 {{"Connection", "close"}});
	}
};
} // namespace

BOOST_AUTO_TEST_CASE(system_reboot_response_validation_and_access_class)
{
	std::atomic_uint reboots{0};
	DeviceEndpoint endpoint([&] { ++reboots; });
	for (const auto& handler : endpoint.server->DeviceService()->Handlers())
		if (handler->name() == "SystemReboot")
			BOOST_CHECK(handler->security_level() == osrv::auth::SECURITY_LEVELS::UNRECOVERABLE);
	for (const auto& request : {"<d:SystemReboot><d:Unexpected/></d:SystemReboot>", "<d:SystemReboot unexpected='true'/>",
															"<d:SystemReboot>unexpected</d:SystemReboot>",
			"<d:SystemReboot/><d:SystemReboot/>"})
	{
		auto response = endpoint.Call(request);
		BOOST_TEST(response->status_code == "400 Bad Request");
		BOOST_TEST(response->content.string().find("ter:InvalidArgVal") != std::string::npos);
	}
	BOOST_TEST(endpoint.Call("<d:SystemReboot/>", "urn:wrong")->status_code == "400 Bad Request");
	BOOST_TEST(reboots.load() == 0u);
	auto response = endpoint.Call();
	BOOST_TEST(response->status_code == "200 OK");
	pt::ptree xml;
	pt::read_xml(response->content, xml);
	BOOST_TEST(xml.get<std::string>("s:Envelope.<xmlattr>.xmlns:tds") == osrv::SERVICE_URI::DEVICE);
	BOOST_TEST(xml.get<std::string>("s:Envelope.s:Body.tds:SystemRebootResponse.tds:Message") ==
						 "Application is rebooting");
	BOOST_TEST(xml.get_child("s:Envelope.s:Body.tds:SystemRebootResponse").size() == 1u);
	// A subsequent request is processed after the send completion callback.
	BOOST_TEST(endpoint.Call("<d:SystemReboot/>", "urn:wrong")->status_code == "400 Bad Request");
	BOOST_TEST(reboots.load() == 1u);
	BOOST_TEST(endpoint.Call("<SystemReboot xmlns='http://www.onvif.org/ver10/device/wsdl'/>")->status_code == "200 OK");
}

BOOST_AUTO_TEST_CASE(system_reboot_unavailable_returns_soap_fault)
{
	DeviceEndpoint endpoint;
	auto response = endpoint.Call();
	BOOST_TEST(response->status_code == "500 Internal Server Error");
	pt::ptree xml;
	pt::read_xml(response->content, xml);
	BOOST_TEST(xml.get<std::string>("s:Envelope.s:Body.s:Fault.s:Code.s:Subcode.s:Value") == "ter:ActionNotSupported");
}

BOOST_AUTO_TEST_CASE(system_reboot_failure_keeps_endpoint_available_for_retry)
{
	std::atomic_uint attempts{0};
	DeviceEndpoint endpoint([&] {
		++attempts;
		throw std::runtime_error("Simulated exec failure");
	});
	BOOST_TEST(endpoint.Call()->status_code == "200 OK");
	BOOST_TEST(endpoint.Call()->status_code == "200 OK");
	BOOST_TEST(endpoint.Call("<d:SystemReboot/>", "urn:wrong")->status_code == "400 Bad Request");
	BOOST_TEST(attempts.load() == 2u);
}

BOOST_AUTO_TEST_CASE(authorization_prevents_reboot_side_effects_before_handler_execution)
{
	std::atomic_uint reboots{0};
	DeviceEndpoint endpoint([&] { ++reboots; }, osrv::AUTH_SCHEME::WSS);
	auto call = [&](const std::string& login, const std::string& password, char nonce, const std::string& operation) {
		const auto created =
				boost::posix_time::to_iso_extended_string(boost::posix_time::microsec_clock::universal_time()) + "Z";
		return endpoint.client->request(
				"POST", "/onvif/device_service",
				"<s:Envelope xmlns:s='http://www.w3.org/2003/05/soap-envelope' "
				"xmlns:d='http://www.onvif.org/ver10/device/wsdl'>"
				"<s:Header><Security xmlns='http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd'>"
				"<UsernameToken><Username>" +
						login + "</Username><Password>" + password + "</Password><Nonce>AAAAAAAAAAA" + nonce +
						"</Nonce><Created "
						"xmlns='http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd'>" +
						created + "</Created></UsernameToken></Security></s:Header><s:Body><d:" + operation +
						"/></s:Body></s:Envelope>",
				{{"Connection", "close"}});
	};
	char nonce = 'A';
	for (const auto& [login, password] : {std::pair{"u", "u1"}, {"o", "o1"}})
	{
		BOOST_TEST(call(login, password, nonce++, "GetDeviceInformation")->status_code == "200 OK");
		BOOST_TEST(call(login, password, nonce++, "GetNetworkProtocols")->status_code == "200 OK");
		auto response = call(login, password, nonce++, "SystemReboot");
		BOOST_TEST(response->status_code == "400 Bad Request");
		BOOST_TEST(response->header.count("WWW-Authenticate") == 0u);
		pt::ptree fault;
		pt::read_xml(response->content, fault);
		BOOST_TEST(fault.get<std::string>("s:Envelope.s:Body.s:Fault.s:Code.s:Value") == "s:Sender");
		BOOST_TEST(fault.get<std::string>("s:Envelope.s:Body.s:Fault.s:Code.s:Subcode.s:Value") == "ter:NotAuthorized");
		BOOST_TEST(reboots.load() == 0u);
	}
	BOOST_TEST(call("a", "a1", nonce++, "SystemReboot")->status_code == "200 OK");
	// The next request runs after response completion, when a permitted reboot invokes its callback.
	BOOST_TEST(call("u", "u1", nonce++, "GetDeviceInformation")->status_code == "200 OK");
	BOOST_TEST(reboots.load() == 1u);
}

BOOST_AUTO_TEST_CASE(network_protocols_reports_listener_configuration_and_access_class)
{
	DeviceEndpoint endpoint;
	auto config = endpoint.server->GetServerConfigs();
	config->http_port_ = "18080";
	config->https_port_ = "18443";
	config->rtsp_port_ = "18554";
	config->advertised_http_port_ = 28080;
	config->advertised_https_port_ = 28443;
	config->advertised_rtsp_port_ = 28554;
	config->enabled_http_port_forwarding = config->enabled_rtsp_port_forwarding = true;
	config->forwarded_http_port = 38080;
	config->forwarded_rtsp_port = 38554;
	bool registered = false;
	for (const auto& handler : endpoint.server->DeviceService()->Handlers())
		if (handler->name() == "GetNetworkProtocols")
		{
			registered = true;
			BOOST_CHECK(handler->security_level() == osrv::auth::SECURITY_LEVELS::READ_SYSTEM);
		}
	BOOST_REQUIRE(registered);
	for (const auto& [http, https] : {std::pair{true, true}, {false, true}, {true, false}})
	{
		config->http_enabled_ = http;
		config->https_enabled_ = https;
		auto response = endpoint.Call("<d:GetNetworkProtocols/>");
		BOOST_REQUIRE(response->status_code == "200 OK");
		pt::ptree xml;
		pt::read_xml(response->content, xml);
		BOOST_TEST(xml.get<std::string>("s:Envelope.<xmlattr>.xmlns:tds") == osrv::SERVICE_URI::DEVICE);
		BOOST_TEST(xml.get<std::string>("s:Envelope.<xmlattr>.xmlns:tt") == "http://www.onvif.org/ver10/schema");
		const auto& entries = xml.get_child("s:Envelope.s:Body.tds:GetNetworkProtocolsResponse");
		BOOST_REQUIRE_EQUAL(entries.size(), 3u);
		unsigned index = 0;
		const std::vector<std::string> names{"HTTP", "HTTPS", "RTSP"};
		const std::vector<int> ports{18080, 18443, 18554};
		const std::vector<bool> enabled{http, https, true};
		for (const auto& [key, entry] : entries)
		{
			BOOST_TEST(key == "tds:NetworkProtocols");
			BOOST_TEST(entry.get<std::string>("tt:Name") == names[index]);
			BOOST_TEST(entry.get<bool>("tt:Enabled") == enabled[index]);
			BOOST_TEST(entry.get<int>("tt:Port") == ports[index]);
			std::vector<std::string> order;
			for (const auto& field : entry)
				order.push_back(field.first);
			const std::vector<std::string> expected{"tt:Name", "tt:Enabled", "tt:Port"};
			BOOST_TEST(order == expected, boost::test_tools::per_element());
			++index;
		}
	}
	BOOST_TEST(endpoint.Call("<GetNetworkProtocols xmlns='http://www.onvif.org/ver10/device/wsdl'/>")->status_code ==
						 "200 OK");
}

BOOST_AUTO_TEST_CASE(network_protocols_rejects_nonempty_and_wrong_namespace_requests)
{
	DeviceEndpoint endpoint;
	for (const auto& request :
			 {"<d:GetNetworkProtocols><d:Unexpected/></d:GetNetworkProtocols>", "<d:GetNetworkProtocols unexpected='true'/>",
				"<d:GetNetworkProtocols>unexpected</d:GetNetworkProtocols>",
				"<d:GetNetworkProtocols/><d:GetNetworkProtocols/>"})
	{
		auto response = endpoint.Call(request);
		BOOST_TEST(response->status_code == "400 Bad Request");
		pt::ptree xml;
		pt::read_xml(response->content, xml);
		BOOST_TEST(xml.get<std::string>("s:Envelope.s:Body.s:Fault.s:Code.s:Value") == "s:Sender");
		BOOST_TEST(xml.get<std::string>("s:Envelope.s:Body.s:Fault.s:Code.s:Subcode.s:Value") == "ter:InvalidArgVal");
	}
	BOOST_TEST(endpoint.Call("<d:GetNetworkProtocols/>", "urn:wrong")->status_code == "400 Bad Request");
	DeviceEndpoint authenticated({}, osrv::AUTH_SCHEME::WSS);
	auto denied = authenticated.Call("<d:GetNetworkProtocols/>");
	BOOST_TEST(denied->status_code == "400 Bad Request");
	BOOST_TEST(denied->content.string().find("ter:NotAuthorized") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(get_services_http_transport_overrides_advertised_scheme_and_preserves_port_overrides)
{
	DeviceEndpoint endpoint;
	auto config = endpoint.server->GetServerConfigs();
	config->advertised_ipv4_ = "192.0.2.8";
	config->http_port_ = "8080";
	config->https_port_ = "8443";
	config->enabled_http_port_forwarding = true;
	config->forwarded_http_port = 18080;
	for (const auto& scheme : {"auto", "http", "https"})
	{
		config->advertised_web_scheme_ = scheme;
		for (unsigned short advertised : {0, 28080})
		{
			config->advertised_http_port_ = advertised;
			const auto expected = std::string("http://192.0.2.8:") + (advertised ? "28080/" : "18080/");
			for (const auto& include : {"false", "true"})
			{
				auto response = endpoint.Call(std::string("<d:GetServices><d:IncludeCapability>") + include +
																			"</d:IncludeCapability></d:GetServices>");
				BOOST_REQUIRE(response->status_code == "200 OK");
				pt::ptree xml;
				pt::read_xml(response->content, xml);
				const auto& services = xml.get_child("s:Envelope.s:Body.tds:GetServicesResponse");
				BOOST_REQUIRE(!services.empty());
				bool media2 = false;
				for (const auto& [key, service] : services)
				{
					const auto uri = service.get<std::string>("tds:XAddr");
					BOOST_TEST(uri.starts_with(expected));
					if (service.get<std::string>("tds:Namespace") == "http://www.onvif.org/ver20/media/wsdl")
					{
						media2 = true;
						BOOST_TEST(uri == expected + "onvif/media2_service");
					}
				}
				BOOST_TEST(media2);
			}
		}
	}
}
