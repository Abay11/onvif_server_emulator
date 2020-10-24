#pragma once

#include "../Types.inl"
#include "HttpDigestHelper.h"

#include <stdexcept>
#include <vector>
#include <chrono>
#include <memory>

namespace osrv
{
	namespace auth
	{
		extern const char TYPE_ADMIN_STR[];
		extern const char TYPE_OPER_STR[];
		extern const char TYPE_USER_STR[];

		
		struct digest_failed : public std::runtime_error
		{
			digest_failed() : runtime_error("HTTP Digest authentication failed!") {}
		};

		enum class USER_TYPE
		{
			ANON = 0,
			USER,
			OPERATOR,
			ADMIN
		};

		enum class SECURITY_LEVELS
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

		struct UserAccount
		{
			std::string login;
			std::string password;
			USER_TYPE type;
		};

		UsersList_t read_system_users(const std::string& /*config_path*/);

		inline bool isUserHasAccess(USER_TYPE user, SECURITY_LEVELS lvl)
		{
			return static_cast<int>(user) >= static_cast<int>(lvl);
		}

		//If could not found Username in users lists, ANON mode will be returned as default
		USER_TYPE get_usertype_by_username(const std::string& /*username*/, const UsersList_t& /*users*/);

	}//auth
}//osrv