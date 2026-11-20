#pragma once

#include "../onvif/Notification.h"

#include <boost/asio.hpp>
#include <boost/signals2/connection.hpp>

#include <functional>
#include <future>
#include <mutex>
#include <thread>

class ILogger;

namespace osrv { class SystemJournal; }

namespace osrv::event
{
class IEventGenerator;

struct EventQueueLimits
{
	std::size_t messages = 256;
	std::size_t bytes = 4 * 1024 * 1024;
};

// A consumer owns this handle. Destruction closes its queue and cancels its wait.
class EventSubscription : public std::enable_shared_from_this<EventSubscription>
{
public:
	struct Batch
	{
		std::deque<NotificationPtr> messages;
		std::size_t dropped = 0;
		bool needs_synchronization = false;
	};

	~EventSubscription();
	Batch Take(std::size_t limit);
	void AsyncWait(boost::asio::io_context& context, std::function<void()> ready);
	void CancelWait();
	void Close();
	bool NeedsSynchronization() const;

private:
	friend class EventSource;
	explicit EventSubscription(EventQueueLimits limits);
	bool Enqueue(NotificationPtr message, std::size_t bytes);
	void Wake();
	void MarkSynchronized();
	struct Entry
	{
		NotificationPtr message;
		std::size_t bytes;
	};
	EventQueueLimits limits_;
	mutable std::mutex mutex_;
	std::deque<Entry> messages_;
	std::size_t bytes_ = 0;
	std::size_t dropped_ = 0;
	bool needs_synchronization_ = false;
	bool closed_ = false;
	std::uint64_t wait_generation_ = 0;
	boost::asio::io_context* wait_context_ = nullptr;
	std::function<void()> ready_;
};

class IEventSource
{
public:
	virtual ~IEventSource() = default;
	virtual std::shared_ptr<EventSubscription> Subscribe(EventQueueLimits limits = {}, bool initial_state = true) = 0;
	virtual void Synchronize(const std::shared_ptr<EventSubscription>& subscription) = 0;
	virtual std::map<std::string, std::string> Namespaces()
	{
		return {};
	}
};

// All generator/state operations run on this object's single worker. Consumers
// only read bounded queues; no consumer callback runs on the publishing path.
class EventSource : public IEventSource
{
public:
	explicit EventSource(const ILogger& logger, std::shared_ptr<SystemJournal> journal = {});
	~EventSource();
	EventSource(const EventSource&) = delete;
	EventSource& operator=(const EventSource&) = delete;
	void AddGenerator(std::shared_ptr<IEventGenerator> generator);
	void Run();
	void SetNamespaces(std::map<std::string, std::string> namespaces);
	std::map<std::string, std::string> Namespaces() override;
	std::shared_ptr<EventSubscription> Subscribe(EventQueueLimits limits = {}, bool initial_state = true) override;
	void Synchronize(const std::shared_ptr<EventSubscription>& subscription) override;
	boost::asio::io_context& GetIoContext();

	template <typename Function> auto Invoke(Function function) -> std::invoke_result_t<Function>
	{
		if (io_context_.get_executor().running_in_this_thread())
			return function();
		auto task = std::make_shared<std::packaged_task<std::invoke_result_t<Function>()>>(std::move(function));
		auto result = task->get_future();
		boost::asio::post(io_context_, [task]() { (*task)(); });
		return result.get();
	}

private:
	void Publish(NotificationMessage message);
	const ILogger& logger_;
	const std::shared_ptr<SystemJournal> journal_;
	boost::asio::io_context io_context_;
	boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_;
	std::thread worker_;
	bool running_ = false;
	std::map<std::string, std::string> namespaces_;
	std::vector<std::shared_ptr<IEventGenerator>> generators_;
	std::vector<boost::signals2::scoped_connection> connections_;
	std::vector<std::weak_ptr<EventSubscription>> subscriptions_;
};
} // namespace osrv::event
