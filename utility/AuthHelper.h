#pragma once

#include "../HttpServerFwd.h"
#include "HttpDigestHelper.h"

#include <chrono>
#include <memory>
#include <stdexcept>
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
	digest_failed() : runtime_error("HTTP Digest authentication failed!")
	{
	}
};

enum class SECURITY_LEVELS : unsigned char
{
	PRE_AUTH = 0,
	READ_SYSTEM = 1,
	READ_MEDIA = 1,
	READ_SYSTEM_SENSITIVE = 2,
	ACTUATE = 2,
	READ_SYSTEM_SECRET = 3,
	WRITE_SYSTEM = 3,
	UNRECOVERABLE = 3,
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

	// by this contants should be read values from config files
	static const std::string LOGIN;
	static const std::string PASS;
	static const std::string TYPE;
};

inline bool isUserHasAccess(USER_TYPE user, SECURITY_LEVELS lvl)
{
	return static_cast<int>(user) >= static_cast<int>(lvl);
}

// If could not found Username in users lists, ANON mode will be returned as default
USER_TYPE get_usertype_by_username(const std::string& /*username*/,
																	 const std::vector<osrv::auth::UserAccount>& /*users*/);

// This converts for ex. "administrator" -> USER_TYPE::ADMIN
USER_TYPE str_to_usertype(const std::string& /*str*/);
} // namespace auth
} // namespace osrv