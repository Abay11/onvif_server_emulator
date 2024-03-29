#include "HttpDigestHelper.h"

#include "../utility/HttpHelper.h"

#include <regex>

namespace utility
{
namespace digest
{
DigestRequestHeader extract_DA(const std::string& www_auth_line)
{
	DigestRequestHeader result;
	result.username = utility::string::search_value(www_auth_line, http::DIGEST_USERNAME);
	result.realm = utility::string::search_value(www_auth_line, http::DIGEST_REALM);
	result.message_qop = utility::string::search_value(www_auth_line, http::DIGEST_MESSAGE_QOP);
	result.algorithm = utility::string::search_value(www_auth_line, http::DIGEST_ALGORITHM);
	result.digest_uri = utility::string::search_value(www_auth_line, http::DIGEST_URI);
	result.nonce = utility::string::search_value(www_auth_line, http::DIGEST_NONCE);
	result.nonce_count = utility::string::search_value(www_auth_line, http::DIGEST_NONCE_COUNT);
	result.cnonce = utility::string::search_value(www_auth_line, http::DIGEST_CNONCE);
	result.response = utility::string::search_value(www_auth_line, http::DIGEST_RESPONSE);
	result.opaque = utility::string::search_value(www_auth_line, http::DIGEST_OPAQUE);

	return result;
}

bool DigestSessionImpl::verifyDigest(const DigestRequestHeader& digestInfo, bool& isStaled)
{
	// TODO: NOW it's just search only for specified username, fix and check full digest verification
	return std::find_if(system_users.begin(), system_users.end(), [&digestInfo](const osrv::auth::UserAccount& user_ac) {
					 return user_ac.login == digestInfo.username;
				 }) != system_users.end();
}
} // namespace digest

std::string utility::string::search_value(const std::string& source, std::string key)
{
	using namespace std;

	// key1="value1", key2 = "value2", key3=value3
	key += "\\s?=\\s?\"?([^,\"\\s]*)\"?";
	regex value_regex(key, regex::flag_type::icase);
	smatch matches;
	if (regex_search(source, matches, value_regex))
		return matches[1].str();

	return std::string();
}

} // namespace utility
