#include <boost/test/unit_test.hpp>

#include "../utility/AuthHelper.h"
#include "../utility/HttpDigestHelper.h"

BOOST_AUTO_TEST_CASE(auth_access)
{
	using namespace osrv::auth;
	BOOST_TEST(true == osrv::auth::isUserHasAccess(USER_TYPE::ANON, SECURITY_LEVELS::PRE_AUTH));
	BOOST_TEST(false == osrv::auth::isUserHasAccess(USER_TYPE::ANON, SECURITY_LEVELS::READ_SYSTEM));
	BOOST_TEST(true == osrv::auth::isUserHasAccess(USER_TYPE::USER, SECURITY_LEVELS::PRE_AUTH));
	BOOST_TEST(true == osrv::auth::isUserHasAccess(USER_TYPE::USER, SECURITY_LEVELS::READ_SYSTEM));
	BOOST_TEST(true == osrv::auth::isUserHasAccess(USER_TYPE::OPERATOR, SECURITY_LEVELS::READ_SYSTEM));
	BOOST_TEST(false == osrv::auth::isUserHasAccess(USER_TYPE::OPERATOR, SECURITY_LEVELS::READ_SYSTEM_SECRET));
	BOOST_TEST(true == osrv::auth::isUserHasAccess(USER_TYPE::ADMIN, SECURITY_LEVELS::WRITE_SYSTEM));
}

BOOST_AUTO_TEST_CASE(read_system_users_func)
{
	using namespace osrv::auth;

	const std::string system_users_path = "../../unit_tests/test_data/system_users_list_test.config";
	try
	{
		auto actual_result = read_system_users(system_users_path);

		BOOST_TEST(3 == actual_result.size());

		auto admin = actual_result.at(0);
		
		BOOST_TEST(admin.login == "a");
		BOOST_TEST(admin.password == "a1");
		BOOST_TEST(true == (admin.type == USER_TYPE::ADMIN));
		
		auto oper = actual_result.at(1);
		BOOST_TEST(oper.login == "o");
		BOOST_TEST(oper.password == "o1");
		BOOST_TEST(true == (oper.type == USER_TYPE::OPERATOR));
		
		auto user = actual_result.at(2);
		BOOST_TEST(user.login == "u");
		BOOST_TEST(user.password == "u1");
		BOOST_TEST(true == (user.type == USER_TYPE::USER));
	}
	catch (const std::exception&)
	{
		BOOST_ASSERT(false);
	}
}
