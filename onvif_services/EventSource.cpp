#include "EventSource.h"

#include "../Logger.h"
#include "pullpoint/event_generators.h"

#include <algorithm>
#include <stdexcept>

namespace osrv::event
{
EventSubscription::EventSubscription(EventQueueLimits limits) : limits_(limits)
{
	if (!limits.messages || !limits.bytes)
		throw std::invalid_argument("Event queue limits must be positive");
}

EventSubscription::~EventSubscription()
{
	Close();
}

EventSubscription::Batch EventSubscription::Take(std::size_t limit)
{
	std::lock_guard lock(mutex_);
	Batch batch;
	batch.dropped = std::exchange(dropped_, 0);
	batch.needs_synchronization = needs_synchronization_;
	while (limit-- && !messages_.empty())
	{
		bytes_ -= messages_.front().bytes;
		batch.messages.push_back(std::move(messages_.front().message));
		messages_.pop_front();
	}
	return batch;
}

void EventSubscription::AsyncWait(boost::asio::io_context& context, std::function<void()> ready)
{
	std::lock_guard lock(mutex_);
	++wait_generation_;
	if (closed_)
		return;
	wait_context_ = &context;
	ready_ = std::move(ready);
	if (!messages_.empty() || dropped_)
		Wake();
}

void EventSubscription::CancelWait()
{
	std::lock_guard lock(mutex_);
	++wait_generation_;
	ready_ = {};
}

void EventSubscription::Close()
{
	std::lock_guard lock(mutex_);
	closed_ = true;
	++wait_generation_;
	ready_ = {};
	messages_.clear();
	bytes_ = 0;
}

bool EventSubscription::NeedsSynchronization() const
{
	std::lock_guard lock(mutex_);
	return needs_synchronization_;
}

void EventSubscription::MarkSynchronized()
{
	std::lock_guard lock(mutex_);
	needs_synchronization_ = false;
}

void EventSubscription::Wake()
{
	// Called with mutex_ held. A wait posts at most one completion; slow readers
	// cannot accumulate an unbounded queue of notification callbacks.
	if (!ready_)
		return;
	auto callback = std::move(ready_);
	ready_ = {};
	auto generation = wait_generation_;
	boost::asio::post(*wait_context_, [weak = weak_from_this(), generation, callback = std::move(callback)]() {
		if (auto self = weak.lock())
		{
			{
				std::lock_guard lock(self->mutex_);
				if (self->closed_ || self->wait_generation_ != generation)
					return;
			}
			callback();
		}
	});
}

bool EventSubscription::Enqueue(NotificationPtr message, std::size_t bytes)
{
	std::lock_guard lock(mutex_);
	if (closed_)
		return true;
	bool retained_all = true;
	if (bytes > limits_.bytes)
	{
		++dropped_;
		retained_all = false;
	}
	else
	{
		while (messages_.size() >= limits_.messages || bytes_ > limits_.bytes - bytes)
		{
			bytes_ -= messages_.front().bytes;
			messages_.pop_front();
			++dropped_;
			retained_all = false;
		}
		messages_.push_back({std::move(message), bytes});
		bytes_ += bytes;
	}
	if (!retained_all)
		needs_synchronization_ = true;
	Wake();
	return retained_all;
}

EventSource::EventSource(const ILogger& logger)
		: logger_(logger), work_(boost::asio::make_work_guard(io_context_)), worker_([this]() { io_context_.run(); })
{
}

EventSource::~EventSource()
{
	Invoke([this]() {
		connections_.clear();
		for (const auto& generator : generators_)
			generator->Stop();
		for (const auto& weak : subscriptions_)
			if (auto subscription = weak.lock())
				subscription->Close();
	});
	work_.reset();
	io_context_.stop();
	worker_.join();
}

void EventSource::AddGenerator(std::shared_ptr<IEventGenerator> generator)
{
	Invoke([this, generator = std::move(generator)]() {
		if (!generator || running_)
			throw std::logic_error("Register non-null generators before EventSource::Run");
		connections_.emplace_back(generator->Connect([this](NotificationMessage message) {
			boost::asio::dispatch(io_context_,
														[this, message = std::move(message)]() mutable { Publish(std::move(message)); });
		}));
		generators_.push_back(generator);
	});
}

void EventSource::Run()
{
	Invoke([this]() {
		if (running_)
			return;
		running_ = true;
		for (const auto& generator : generators_)
			generator->Run();
	});
}

boost::asio::io_context& EventSource::GetIoContext()
{
	return io_context_;
}

std::shared_ptr<EventSubscription> EventSource::Subscribe(EventQueueLimits limits, bool initial_state)
{
	return Invoke([this, limits, initial_state]() {
		std::erase_if(subscriptions_, [](const auto& weak) { return weak.expired(); });
		auto subscription = std::shared_ptr<EventSubscription>(new EventSubscription(limits));
		subscriptions_.push_back(subscription);
		if (initial_state)
			Synchronize(subscription);
		return subscription;
	});
}

void EventSource::Synchronize(const std::shared_ptr<EventSubscription>& subscription)
{
	if (!subscription)
		throw std::invalid_argument("Cannot synchronize a null event subscription");
	Invoke([this, subscription]() {
		bool complete = true;
		for (const auto& generator : generators_)
			for (auto& message : generator->GenerateSynchronizationEvent())
			{
				// Transient notifications do not have an initialization state.
				if (message.property_operation != "Initialized")
					continue;
				auto bytes = notification_size(message);
				complete =
						subscription->Enqueue(std::make_shared<const NotificationMessage>(std::move(message)), bytes) && complete;
			}
		if (complete)
			subscription->MarkSynchronized();
		else
			logger_.Warn("Event property snapshot exceeds consumer queue limits; resynchronization still required");
	});
}

void EventSource::Publish(NotificationMessage message)
{
	auto bytes = notification_size(message);
	auto immutable = std::make_shared<const NotificationMessage>(std::move(message));
	std::erase_if(subscriptions_, [](const auto& weak) { return weak.expired(); });
	for (const auto& weak : subscriptions_)
		if (auto subscription = weak.lock())
		{
			auto already_lost = subscription->NeedsSynchronization();
			if (!subscription->Enqueue(immutable, bytes) && !already_lost)
				logger_.Warn("Event consumer queue overflow; notifications dropped and property resynchronization required");
		}
}
} // namespace osrv::event
