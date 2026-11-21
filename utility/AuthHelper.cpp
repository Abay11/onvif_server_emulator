#include "AuthHelper.h"

#include <algorithm>
#include <fstream>
#include <vector>

#include <boost/property_tree/json_parser.hpp>

const std::string osrv::auth::UserAccount::LOGIN = "login";
const std::string osrv::auth::UserAccount::PASS = "password";
const std::string osrv::auth::UserAccount::TYPE = "type";

namespace osrv
{
namespace auth
{
const char TYPE_ADMIN_STR[] = "administrator";
const char TYPE_OPER_STR[] = "operator";
const char TYPE_USER_STR[] = "user";

bool isUserHasAccess(USER_TYPE user, SECURITY_LEVELS level)
{
	if (user != USER_TYPE::ANON && user != USER_TYPE::USER && user != USER_TYPE::OPERATOR && user != USER_TYPE::ADMIN)
		return false;
	switch (level)
	{
	case SECURITY_LEVELS::PRE_AUTH:
		return true;
	case SECURITY_LEVELS::READ_SYSTEM:
	case SECURITY_LEVELS::READ_MEDIA:
		return user != USER_TYPE::ANON;
	case SECURITY_LEVELS::READ_SYSTEM_SENSITIVE:
	case SECURITY_LEVELS::ACTUATE:
		return user == USER_TYPE::OPERATOR || user == USER_TYPE::ADMIN;
	case SECURITY_LEVELS::READ_SYSTEM_SECRET:
	case SECURITY_LEVELS::WRITE_SYSTEM:
	case SECURITY_LEVELS::UNRECOVERABLE:
		return user == USER_TYPE::ADMIN;
	}
	return false;
}

USER_TYPE get_usertype_by_username(const std::string& username, const std::vector<osrv::auth::UserAccount>& users)
{
	auto it = std::find_if(users.begin(), users.end(), [&username, &users](const UserAccount& user) {
		return user.enabled && user.login == username;
	});

	if (it != users.end())
	{
		return it->type;
	}

	return USER_TYPE::ANON;
}

osrv::auth::USER_TYPE str_to_usertype(const std::string& str)
{
	using namespace osrv::auth;

	if (str == TYPE_ADMIN_STR)
		return USER_TYPE::ADMIN;

	if (str == TYPE_OPER_STR)
		return USER_TYPE::OPERATOR;

	if (str == TYPE_USER_STR)
		return USER_TYPE::USER;

	throw std::runtime_error("Uknown user type");
}

} // namespace auth
} // namespace osrv
