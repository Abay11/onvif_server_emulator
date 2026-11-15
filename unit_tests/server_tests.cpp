#include <boost/test/unit_test.hpp>

#include "../Server.h"
#include "../onvif_services/physical_components/IDigitalInput.h"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <memory>
#include <cstdio>

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
    const std::string path = "network-validation.config";
    pt::write_json(path, tree);
    const auto cfg = osrv::read_server_configs(path);
    BOOST_TEST(cfg->HttpAddress() == "http://192.168.1.10:18080/");
    BOOST_TEST(cfg->RtspAddress() == "rtsp://192.168.1.10:18554/");
    BOOST_TEST(cfg->rtp_port_min_ == 51000);
    BOOST_TEST(cfg->rtp_port_max_ == 51099);
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
}
