#pragma once

#include <chrono>
#include <map>
#include <memory>
#include <string>

#include "../HttpServerFwd.h"
#include "AuthHelper.h"

#include <boost/optional/optional_io.hpp>

namespace osrv::auth
{
struct UserAccount;
}

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

	std::string to_string() const
	{
		return "Digest realm=\"" + realm + "\"" + ", qop=\"" + qop + "\"" + ", nonce=\"" + nonce + "\"";
	}
};

struct DigestRequestHeader
{
	std::string username;
	std::string realm;
	std::string nonce;
	std::string digest_uri;

	// this fialds are optional actually, so they could be empty
	std::string response;
	std::string algorithm;
	std::string cnonce;
	std::string opaque;
	std::string message_qop;
	std::string nonce_count;
};

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
	std::map<std::string, std::chrono::milliseconds> nonce_pool;

	const std::string realm_;
	const std::string qop_;
};

class DigestSessionImpl : public utility::digest::IDigestSession
{

public:
	DigestSessionImpl(const std::string& realm = "Realm", const std::string& qop = "auth") : IDigestSession(realm, qop)
	{
	}

	DigestResponseHeader generateDigest() override
	{
		DigestResponseHeader result;
		result.nonce = "need_to_fix_nonce"; // FIX: generate normally
		result.realm = realm_;
		result.qop = qop_;

		return result;
	}

	bool verifyDigest(const DigestRequestHeader& digestInfo, bool& isStaled) override;
};

} // namespace digest
} // namespace utility