#include "../utility/StreamTransport.h"
#include "../utility/RtspHttpTunnel.h"
#include "../utility/MetadataConfiguration.h"
#include <boost/test/unit_test.hpp>

#include "../Server.h"
#include "../onvif_services/physical_components/IDigitalInput.h"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <memory>
#include <cstdio>
#include <filesystem>

namespace pt = boost::property_tree;
BOOST_AUTO_TEST_CASE(read_server_configs_func)
{
	const std::string server_configs_path = "../../unit_tests/test_data/system_users_list_test.config";
	std::shared_ptr<osrv::ServerConfigs> actual_configs;
	try
	{
		actual_configs = osrv::read_server_configs(server_configs_path);
	}
	catch (const std::exception& e)
	{
		BOOST_TEST("" == e.what());
		BOOST_ASSERT(false);
	}

	auto expected_auth_scheme = osrv::AUTH_SCHEME::DIGEST;
	BOOST_TEST(true == (actual_configs->auth_scheme_ == expected_auth_scheme));

	auto actual_system_users = actual_configs->system_users_;

	BOOST_TEST(3 == actual_system_users.size());

	auto admin = actual_system_users.at(0);

	using namespace osrv::auth;

	BOOST_TEST(admin.login == "a");
	BOOST_TEST(admin.password == "a1");
	BOOST_TEST(true == (admin.type == USER_TYPE::ADMIN));

	auto oper = actual_system_users.at(1);
	BOOST_TEST(oper.login == "o");
	BOOST_TEST(oper.password == "o1");
	BOOST_TEST(true == (oper.type == USER_TYPE::OPERATOR));

	auto user = actual_system_users.at(2);
	BOOST_TEST(user.login == "u");
	BOOST_TEST(user.password == "u1");
	BOOST_TEST(true == (user.type == USER_TYPE::USER));
}

BOOST_AUTO_TEST_CASE(read_digital_inputs_func)
{
	const std::string config_example =
			R"(
			{
				"DigitalInputs":
				[
					{
						"Token": "digital_input0",
						"GenerateEvent": true,
						"InitialState":true
					},
					{
						"Token": "digital_input1",
						"GenerateEvent": false,
						"InitialState":false
					}
				]
			}
		)";
	std::stringstream ss(config_example);
	pt::ptree configs_node;
	pt::json_parser::read_json(ss, configs_node);

	auto result = osrv::read_digital_inputs(configs_node.get_child("DigitalInputs"));
	BOOST_TEST(2 == result.size());
	auto di0 = result[0];
	BOOST_TEST("digital_input0" == di0->GetToken());
	BOOST_TEST(true == di0->IsEnabled());
	auto di1 = result[1];
	BOOST_TEST("digital_input1" == di1->GetToken());
	BOOST_TEST(false == di1->IsEnabled());
}

BOOST_AUTO_TEST_CASE(advertised_endpoints_are_independent_of_bind_addresses)
{
    osrv::ServerConfigs cfg{};
    cfg.ipv4_address_ = "0.0.0.0";
    cfg.http_port_ = "8080";
    cfg.rtsp_port_ = "8554";
    cfg.advertised_ipv4_ = "192.168.1.10";
    BOOST_TEST(cfg.HttpAddress() == "http://192.168.1.10:8080/");
    BOOST_TEST(cfg.RtspAddress() == "rtsp://192.168.1.10:8554/");
    cfg.enabled_http_port_forwarding = true;
    cfg.forwarded_http_port = 1080;
    cfg.enabled_rtsp_port_forwarding = true;
    cfg.forwarded_rtsp_port = 1554;
    BOOST_TEST(cfg.HttpAddress() == "http://192.168.1.10:1080/");
    BOOST_TEST(cfg.RtspAddress() == "rtsp://192.168.1.10:1554/");
    cfg.advertised_http_port_ = 18080;
    cfg.advertised_rtsp_port_ = 18554;
    cfg.advertised_pullpoint_port_ = 15550;
    BOOST_TEST(cfg.HttpAddress() == "http://192.168.1.10:18080/");
    BOOST_TEST(cfg.RtspAddress() == "rtsp://192.168.1.10:18554/");
    BOOST_TEST(cfg.PullPointAddress(5550, true) == cfg.HttpAddress());
    BOOST_TEST(cfg.PullPointAddress(5550, false) == "http://192.168.1.10:15550/");
}

BOOST_AUTO_TEST_CASE(advertised_https_can_be_selected_with_both_listeners_enabled)
{
    osrv::ServerConfigs cfg{};
    cfg.http_port_ = "8080";
    cfg.advertised_web_scheme_ = "https";
    cfg.advertised_https_port_ = 19443;
    BOOST_TEST(cfg.http_enabled_);
    BOOST_TEST(cfg.https_enabled_);
    BOOST_TEST(cfg.HttpAddress() == "https://127.0.0.1:19443/");
    BOOST_TEST(cfg.PlainHttpAddress() == "http://127.0.0.1:8080/");
    BOOST_TEST(cfg.PullPointAddress(5550, true) == cfg.HttpAddress());
    BOOST_TEST(cfg.PullPointAddress(5550, false) == "https://127.0.0.1:5550/");
}

BOOST_AUTO_TEST_CASE(network_config_parsing_and_validation)
{
    pt::ptree tree;
    pt::read_json("../../unit_tests/test_data/system_users_list_test.config", tree);
    tree.put("advertised.ipv4", "192.168.1.10");
    tree.put("advertised.http_port", 18080);
    tree.put("advertised.rtsp_port", 18554);
    tree.put("advertised.pullpoint_port", 15550);
    tree.put("rtp.port_min", 51000);
    tree.put("rtp.port_max", 51099);
    tree.put("rtsp_metadata.enabled", true);
    const std::string path = "network_validation.config";
    std::filesystem::copy_file("../../server_configs/users.config", "users.config", std::filesystem::copy_options::overwrite_existing);
    pt::write_json(path, tree);
    const auto cfg = osrv::read_server_configs(path);
    BOOST_TEST(cfg->HttpAddress() == "http://192.168.1.10:18080/");
    BOOST_TEST(cfg->RtspAddress() == "rtsp://192.168.1.10:18554/");
    BOOST_TEST(cfg->rtp_port_min_ == 51000);
    BOOST_TEST(cfg->rtp_port_max_ == 51099);
    BOOST_TEST(cfg->rtsp_metadata_enabled_);
    BOOST_TEST(cfg->http_enabled_);
    BOOST_TEST(cfg->https_enabled_);
    BOOST_TEST(cfg->https_port_ == "8443");
    tree.put("http.enabled", false);
    tree.put("addresses.https_port", 9443);
    tree.put("advertised.https_port", 19443);
    pt::write_json(path, tree);
    auto secure = osrv::read_server_configs(path);
    BOOST_TEST(secure->HttpAddress() == "https://192.168.1.10:19443/");
    BOOST_TEST(secure->PullPointAddress(5550, true) == secure->HttpsAddress());
    BOOST_TEST(secure->PullPointAddress(5550, false) == "https://192.168.1.10:15550/");
    tree.put("https.enabled", false);
    pt::write_json(path, tree);
    BOOST_CHECK_THROW(osrv::read_server_configs(path), std::runtime_error);
    tree.put("http.enabled", true);
    tree.put("https.enabled", true);
    tree.put("addresses.https_port", tree.get<std::string>("addresses.http_port"));
    pt::write_json(path, tree);
    BOOST_CHECK_THROW(osrv::read_server_configs(path), std::runtime_error);
    tree.put("addresses.https_port", 8443);
    tree.put("https.certificate", "missing.pem");
    pt::write_json(path, tree);
    BOOST_CHECK_THROW(osrv::read_server_configs(path), std::runtime_error);
    tree.put("https.certificate", "");
    tree.put("advertised.web_scheme", "ftp");
    pt::write_json(path, tree);
    BOOST_CHECK_THROW(osrv::read_server_configs(path), std::runtime_error);
    tree.put("advertised.web_scheme", "https");
    tree.put("https.enabled", false);
    pt::write_json(path, tree);
    BOOST_CHECK_THROW(osrv::read_server_configs(path), std::runtime_error);
    tree.put("advertised.web_scheme", "auto");
    tree.put("https.enabled", true);
    // Missing settings use the same enabled default as the shipped configuration.
    tree.erase("rtsp_metadata");
    pt::write_json(path, tree);
    BOOST_TEST(osrv::read_server_configs(path)->rtsp_metadata_enabled_);
    BOOST_TEST(osrv::ServerConfigs{}.rtsp_metadata_enabled_);
    tree.put("rtsp_metadata.enabled", false);
    pt::write_json(path, tree);
    BOOST_TEST(!osrv::read_server_configs(path)->rtsp_metadata_enabled_);

    for (const auto* invalid_ip : {"0.0.0.0", "239.255.255.250", "255.255.255.255", "not-an-ip"})
    {
        tree.put("advertised.ipv4", invalid_ip);
        pt::write_json(path, tree);
        BOOST_CHECK_THROW(osrv::read_server_configs(path), std::exception);
    }
    tree.put("advertised.ipv4", "192.168.1.10");
    tree.put("rtp.port_min", 51001);
    pt::write_json(path, tree);
    BOOST_CHECK_THROW(osrv::read_server_configs(path), std::runtime_error);
    tree.put("rtp.port_min", 51000);
    tree.put("rtp.port_max", 50999);
    pt::write_json(path, tree);
    BOOST_CHECK_THROW(osrv::read_server_configs(path), std::runtime_error);
    std::remove(path.c_str());
    std::remove("users.config");
}

BOOST_AUTO_TEST_CASE(stream_transport_uses_request_security_and_advertised_ports)
{
	osrv::ServerConfigs config;
	config.advertised_ipv4_ = "192.0.2.8";
	config.http_port_ = "8080";
	config.rtsp_port_ = "8554";
	config.advertised_http_port_ = 18080;
	config.advertised_https_port_ = 18443;
	config.advertised_web_scheme_ = "https";
	pt::ptree operation;
	operation.put("m:Protocol", "RtspOverHttp");
	BOOST_TEST(osrv::StreamAddress(config, operation, true, false) == "http://192.0.2.8:18080/");
	BOOST_TEST(osrv::StreamAddress(config, operation, true, true) == "https://192.0.2.8:18443/");
	operation.put("m:Protocol", "RtspUnicast");
	BOOST_TEST(osrv::StreamAddress(config, operation, true, true) == "rtsp://192.0.2.8:8554/");
	operation.put("m:Protocol", "RtspsUnicast");
	BOOST_CHECK_THROW(osrv::StreamAddress(config, operation, true, true), osrv::metadata::Fault);
	operation.clear();
	operation.put("m:StreamSetup.tt:Stream", "RTP-Unicast");
	operation.put("m:StreamSetup.tt:Transport.tt:Protocol", "HTTP");
	BOOST_TEST(osrv::StreamAddress(config, operation, false, true) == "https://192.0.2.8:18443/");
	config.https_enabled_ = false;
	BOOST_CHECK_THROW(osrv::StreamAddress(config, operation, false, true), osrv::metadata::Fault);
	operation.put("m:StreamSetup.tt:Transport.tt:Protocol", "HTTPS");
	BOOST_CHECK_THROW(osrv::StreamAddress(config, operation, false, false), osrv::metadata::Fault);
	operation.put("m:StreamSetup.tt:Transport.tt:Protocol", "UDP");
	operation.put("m:StreamSetup.tt:Stream", "RTP-Multicast");
	BOOST_CHECK_THROW(osrv::StreamAddress(config, operation, false, false), osrv::metadata::Fault);
}

BOOST_AUTO_TEST_CASE(rtsp_http_tunnel_header_detection)
{
	BOOST_TEST(osrv::IsRtspHttpTunnel(
			"GET /Live&HighStream HTTP/1.0\r\nx-sessioncookie: abc\r\nAccept: application/x-rtsp-tunnelled\r\n\r\n"));
	BOOST_TEST(osrv::IsRtspHttpTunnel(
			"POST /Live&HighStream HTTP/1.0\r\nX-SessionCookie: abc\r\nContent-Length: 32767\r\n\r\n"));
	BOOST_TEST(!osrv::IsRtspHttpTunnel("GET /admin/ HTTP/1.1\r\n\r\n"));
	BOOST_TEST(!osrv::IsRtspHttpTunnel("POST / HTTP/1.0\r\nx-sessioncookie: \r\n\r\n"));
	BOOST_TEST(!osrv::IsRtspHttpTunnel("DELETE / HTTP/1.0\r\nx-sessioncookie: abc\r\n\r\n"));
}

BOOST_AUTO_TEST_CASE(rtsp_http_tunnel_does_not_redirect_clients_to_internal_endpoint)
{
	const std::string response =
			"HTTP/1.0 200 OK\r\nContent-Type: application/x-rtsp-tunnelled\r\n"
			"X-Server-IP-Address: 127.0.0.1\r\nCache-Control: no-cache\r\n\r\nRTSP/1.0 200 OK\r\n\r\n";
	const std::string expected = "HTTP/1.0 200 OK\r\nContent-Type: application/x-rtsp-tunnelled\r\n"
															 "Cache-Control: no-cache\r\n\r\nRTSP/1.0 200 OK\r\n\r\n";
	BOOST_TEST(osrv::RtspHttpTunnelResponse(response) == expected);
	BOOST_TEST(osrv::RtspHttpTunnelResponse(expected) == expected);
}
