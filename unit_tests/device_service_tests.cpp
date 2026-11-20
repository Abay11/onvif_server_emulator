#include "../Server.h"
#include "../Simple-Web-Server/client_http.hpp"
#include "../include/LoggerFactories.h"
#include "../onvif/OnvifRequest.h"
#include "../onvif_services/IOnvifService.h"
#include <atomic>
#include <future>
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
	DeviceEndpoint(std::function<void()> reboot = {}) : server(std::make_shared<DeviceTestServer>(path, reboot))
	{
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
