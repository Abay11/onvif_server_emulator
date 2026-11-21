#include "WebAuthentication.h"
#include <algorithm>
#include <openssl/crypto.h>
#include <openssl/rand.h>

namespace osrv
{
WebAuthentication::WebAuthentication(std::vector<auth::UserAccount> users, Clock clock)
		: users_(std::move(users)), clock_(std::move(clock))
{
}
std::string WebAuthentication::Login(const std::string& login, const std::string& password)
{
	const auto found = std::find_if(users_.begin(), users_.end(), [&](const auto& user) {
		return user.enabled && user.type == auth::USER_TYPE::ADMIN && user.login == login &&
					 user.password.size() == password.size() &&
					 CRYPTO_memcmp(user.password.data(), password.data(), password.size()) == 0;
	});
	if (found == users_.end())
		return {};
	unsigned char bytes[32];
	if (RAND_bytes(bytes, sizeof(bytes)) != 1)
		throw std::runtime_error("Session creation failed");
	std::string token;
	for (auto byte : bytes)
	{
		token += "0123456789abcdef"[byte >> 4];
		token += "0123456789abcdef"[byte & 15];
	}
	std::lock_guard lock(mutex_);
	const auto now = clock_();
	std::erase_if(sessions_, [&](const auto& entry) { return entry.second.expires <= now; });
	if (sessions_.size() >= 1024)
		throw std::runtime_error("Session capacity reached");
	sessions_.emplace(token, Session{login, now + std::chrono::hours(8)});
	return token;
}
std::string WebAuthentication::Username(const std::string& token)
{
	std::lock_guard lock(mutex_);
	const auto found = sessions_.find(token);
	if (found == sessions_.end())
		return {};
	if (found->second.expires <= clock_())
	{
		sessions_.erase(found);
		return {};
	}
	return found->second.login;
}
void WebAuthentication::Logout(const std::string& token)
{
	std::lock_guard lock(mutex_);
	sessions_.erase(token);
}
} // namespace osrv
