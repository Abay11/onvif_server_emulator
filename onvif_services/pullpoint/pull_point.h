#pragma once

#include "../../HttpServerFwd.h"
#include "../../Logger.h"
#include "../../HttpServer.h"
#include "../../utility/DateTime.hpp"
#include "../EventSource.h"

namespace osrv::event
{
class SubscriptionFault : public std::runtime_error
{
public:
	SubscriptionFault(std::string element, const std::string& reason)
			: std::runtime_error(reason), element(std::move(element))
	{
	}
	std::string element;
};
std::string serialize_subscription_fault(const SubscriptionFault& fault);

class PullPoint : public std::enable_shared_from_this<PullPoint>
{
public:
	using pull_messages_handler_t =
			std::function<void(const std::string&, std::deque<NotificationMessage>&&, std::shared_ptr<HttpServer::Response>)>;
	PullPoint(std::string reference, std::shared_ptr<EventSource> source, const ILogger& logger);
	~PullPoint();
	std::string GetSubscriptionReference() const;
	std::string GetLastRenew() const;
	std::string GetTerminationTime() const;
	void PullMessages(pull_messages_handler_t handler, std::shared_ptr<HttpServer::Response> response, int timeout = 60,
										std::size_t limit = 50);
	void SetSynchronizationPoint();
	void Renew(std::function<void()> expired, std::chrono::seconds lease = std::chrono::seconds(60));
	void Close();
	bool IsClosed() const
	{
		return closed_;
	}

private:
	void Respond();
	const ILogger& logger_;
	std::shared_ptr<EventSource> source_;
	std::shared_ptr<EventSubscription> subscription_;
	std::string reference_;
	boost::asio::steady_timer wait_timer_;
	boost::asio::steady_timer expiry_timer_;
	boost::posix_time::ptime current_time_;
	std::chrono::seconds lease_{60};
	std::size_t message_limit_ = 50;
	bool closed_ = false;
	pull_messages_handler_t handler_;
	std::shared_ptr<HttpServer::Response> response_;
};

using PullPoints_t = std::vector<std::shared_ptr<PullPoint>>;

class NotificationsManager
{
public:
	NotificationsManager(const ILogger& logger, const std::map<std::string, std::string>& namespaces,
											 std::shared_ptr<EventSource> source);
	~NotificationsManager();
	std::shared_ptr<PullPoint> CreatePullPoint();
	void PullMessages(std::shared_ptr<HttpServer::Response> response, const std::string& reference,
										const std::string& message_id, int timeout, int limit);
	void SetSynchronizationPoint(const std::string& reference);
	void Unsubscribe(const std::string& reference);
	void Renew(std::shared_ptr<HttpServer::Response> response, const std::string& reference,
						 const std::string& message_id);
	void Run();
	void AddGenerator(std::shared_ptr<IEventGenerator> generator);
	boost::asio::io_context& GetIoContext();

private:
	std::shared_ptr<PullPoint> Find(const std::string& reference);
	void RenewLease(const std::shared_ptr<PullPoint>& point, int pending_timeout = 0);
	void do_pullmessages_response(const std::string& reference, const std::string& message_id,
																std::deque<NotificationMessage>&& events,
																std::shared_ptr<HttpServer::Response> response);
	const ILogger& logger_;
	std::map<std::string, std::string> namespaces_;
	std::shared_ptr<EventSource> source_;
	PullPoints_t pullpoints_;
	std::uint64_t next_id_ = 0;
};

bool compare_subscription_references(const std::string& full_reference, const std::string& reference);
boost::property_tree::ptree serialize_notification_messages(const std::deque<NotificationMessage>& messages,
																														const std::string& subscription_reference);
PullPoints_t::const_iterator find_pullpoint(const PullPoints_t& points, const std::string& reference);
} // namespace osrv::event
