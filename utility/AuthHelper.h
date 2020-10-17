#pragma once

#include <stdexcept>

namespace osrv
{
	namespace auth
	{

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

		inline bool isUserHasAccess(USER_TYPE user, SECURITY_LEVELS lvl)
		{
			return static_cast<int>(user) >= static_cast<int>(lvl);
		}

	}//osrv
}