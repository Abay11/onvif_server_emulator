#pragma once

#include "../HttpServerFwd.h"

#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace osrv
{
namespace auth
{
extern const char TYPE_ADMIN_STR[];
extern const char TYPE_OPER_STR[];
extern const char TYPE_USER_STR[];

struct digest_failed : public std::runtime_error
{
	digest_failed() : runtime_error("Authentication or access check failed!")
	{
	}
};

// Distinct ONVIF access classes; their numeric values do not encode permissions.
enum class SECURITY_LEVELS : unsigned char
{
	PRE_AUTH,
	READ_SYSTEM,
	READ_MEDIA,
	READ_SYSTEM_SENSITIVE,
	ACTUATE,
	READ_SYSTEM_SECRET,
	WRITE_SYSTEM,
	UNRECOVERABLE,
};

enum class USER_TYPE : unsigned char
{
	ANON = 0,
	USER,
	OPERATOR,
	ADMIN
};

struct UserAccount
{
	std::string login;
	std::string password;
	USER_TYPE type;
	bool enabled = true;

	// by this contants should be read values from config files
	static const std::string LOGIN;
	static const std::string PASS;
	static const std::string TYPE;
};

// ONVIF Core 26.06, section 5.9.4, default access policy. Unknown values are denied.
bool isUserHasAccess(USER_TYPE user, SECURITY_LEVELS lvl);

// If could not found Username in users lists, ANON mode will be returned as default
USER_TYPE get_usertype_by_username(const std::string& /*username*/,
																	 const std::vector<osrv::auth::UserAccount>& /*users*/);

// This converts for ex. "administrator" -> USER_TYPE::ADMIN
USER_TYPE str_to_usertype(const std::string& /*str*/);
} // namespace auth
} // namespace osrv
