#pragma once

#include <algorithm>
#include <boost/property_tree/ptree.hpp>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "../HttpServerFwd.h"
#include "AuthHelper.h"

#include <boost/optional/optional_io.hpp>

namespace utility
{
namespace string
{
/*
Search for associative key-value expressions and return a value matches to a key,
expected that the key values are unique into the passed source
If not found a key, empty value is returned
example: key=value
*/
std::string search_value(const std::string& /*source*/, std::string /*key*/);
} // namespace string

namespace digest
{
struct DigestResponseHeader
{
	std::string realm;
	std::string nonce;
	std::string qop;
	std::string algorithm = "MD5";
	bool stale = false;

	std::string to_string() const
	{
		return std::string("Digest algorithm=") + algorithm + (stale ? ", stale=true" : "") + ", realm=\"" + realm + "\"" +
					 ", qop=\"" + qop + "\"" + ", nonce=\"" + nonce + "\"";
	}
};

struct DigestRequestHeader
{
	std::string username;
	std::string realm;
	std::string nonce;
	std::string digest_uri;

	// Actual HTTP method and target are supplied by the request dispatcher.
	std::string method;
	std::string target;
	std::string response;
	std::string algorithm;
	std::string cnonce;
	std::string opaque;
	std::string message_qop;
	std::string nonce_count;
};

bool hasSecurityHeader(const boost::property_tree::ptree& soap);

DigestRequestHeader extract_DA(const std::string& /*www_auth_line*/);

class IDigestSession
{
public:
	// using std::vector<osrv::auth::UserAccount> = std::vector<osrv::auth::UserAccount>;

	// should generate a new nonce and add it to the pool
	virtual DigestResponseHeader generateDigest() = 0;

	// returns result of verification
	// when result is false, implementation should indicate
	// whether a nonce is staled or not using @isStaled flag
	virtual bool verifyDigest(const DigestRequestHeader& digestInfo, bool& isStaled) = 0;
	virtual std::string verifyUsernameToken(const boost::property_tree::ptree& soap) = 0;
	std::string challenges(bool stale = false);

	void set_users_list(const std::vector<osrv::auth::UserAccount>& users_list)
	{
		system_users.resize(users_list.size());
		std::copy(users_list.begin(), users_list.end(), system_users.begin());
	}

	const std::vector<osrv::auth::UserAccount>& get_users_list() const
	{
		return system_users;
	}

protected:
	IDigestSession(const std::string& realm, const std::string& qop) : realm_(realm), qop_(qop)
	{
	}

	std::vector<osrv::auth::UserAccount> system_users;

	// the pool is used to hold all generated nonce
	struct NonceState
	{
		std::chrono::steady_clock::time_point expires;
		std::map<std::string, unsigned long> counts;
	};
	std::map<std::string, NonceState> nonce_pool;
	std::map<std::string, std::chrono::steady_clock::time_point> token_nonces;
	std::mutex mutex_;

	const std::string realm_;
	const std::string qop_;
};

class DigestSessionImpl : public utility::digest::IDigestSession
{

public:
	DigestSessionImpl(const std::string& realm = "Realm", const std::string& qop = "auth") : IDigestSession(realm, qop)
	{
	}

	DigestResponseHeader generateDigest() override;
	bool verifyDigest(const DigestRequestHeader& digestInfo, bool& isStaled) override;
	std::string verifyUsernameToken(const boost::property_tree::ptree& soap) override;
};

} // namespace digest
} // namespace utility
