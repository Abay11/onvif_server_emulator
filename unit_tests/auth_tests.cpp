#include <boost/test/unit_test.hpp>

#include "../utility/AuthHelper.h"

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
