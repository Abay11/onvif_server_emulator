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

BOOST_AUTO_TEST_CASE(get_usertype_by_username_func)
{
	using namespace osrv::auth;
	std::vector<osrv::auth::UserAccount> users;
	const std::string username = "agent007";
	users.emplace_back(UserAccount{ username, "", USER_TYPE::ADMIN });
	auto res = get_usertype_by_username(username, users);
	BOOST_TEST(true == (res == USER_TYPE::ADMIN));

	std::string username2 = "no_such_user";
	auto res2 = get_usertype_by_username(username2, users);
	BOOST_TEST(true == (res2 == USER_TYPE::ANON));
}
