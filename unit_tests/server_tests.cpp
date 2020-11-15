#include <boost/test/unit_test.hpp>

#include "../Server.h"

BOOST_AUTO_TEST_CASE(read_server_configs_func)
{
	const std::string server_configs_path = "../../unit_tests/test_data/system_users_list_test.config";
	osrv::ServerConfigs actual_configs;
	try {
		actual_configs = osrv::read_server_configs(server_configs_path);
	}
	catch (const std::exception&)
	{
		BOOST_ASSERT(false);
	}
	
	auto expected_auth_scheme = osrv::AUTH_SCHEME::DIGEST;
	BOOST_TEST(true == (actual_configs.auth_scheme_ == expected_auth_scheme));

	auto actual_system_users = actual_configs.system_users_;
	
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
