#pragma once
#include "../utility/AuthHelper.h"
#include <chrono>
#include <functional>
#include <map>
#include <mutex>

namespace osrv
{
// Browser sessions use the same startup account snapshot as ONVIF.
class WebAuthentication
{
public:
	using Clock = std::function<std::chrono::steady_clock::time_point()>;
	explicit WebAuthentication(std::vector<auth::UserAccount> users, Clock clock = std::chrono::steady_clock::now);
	std::string Login(const std::string& login, const std::string& password);
	std::string Username(const std::string& token);
	void Logout(const std::string& token);

private:
	struct Session
	{
		std::string login;
		std::chrono::steady_clock::time_point expires;
	};
	std::vector<auth::UserAccount> users_;
	Clock clock_;
	std::map<std::string, Session> sessions_;
	std::mutex mutex_;
};
} // namespace osrv
