#include "AuthHelper.h"

#include <fstream>
#include <vector>

#include <boost\property_tree\json_parser.hpp>

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

namespace osrv
{
	namespace auth
	{
		const char TYPE_ADMIN_STR[] = "administrator";
		const char TYPE_OPER_STR[] = "operator";
		const char TYPE_USER_STR[] = "user";

		UsersList_t osrv::auth::read_system_users(const std::string& config_path)
		{
			UsersList_t result;

			std::ifstream configs_file(config_path);
			if (!configs_file.is_open())
				throw std::runtime_error("Could not read a config file");

			namespace pt = boost::property_tree;
			pt::ptree configs_tree;
			pt::read_json(configs_file, configs_tree);

			auto users_node = configs_tree.get_child("users");
			if(users_node.empty())
				throw std::runtime_error("Could not read Users list");

			const static char LOGIN[] = "login";
			const static char PASS[] = "password";
			const static char TYPE[] = "type";

			for (auto user : users_node)
			{
				result.emplace_back(UserAccount{
					user.second.get<std::string>(LOGIN),
					user.second.get<std::string>(PASS),
					str_to_usertype(user.second.get<std::string>(TYPE))
				});
			}

			return result;
		}
	}
}
