#include "AuthHelper.h"

#include <fstream>
#include <vector>

#include <boost\property_tree\json_parser.hpp>

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

		USER_TYPE get_usertype_by_username(const std::string& username, const UsersList_t& users)
		{
			auto it = std::find_if(users.begin(), users.end(),
				[&username, &users](const UserAccount& user) {
					return user.login == username;
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



	}
}
