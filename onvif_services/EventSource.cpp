#include "EventSource.h"

#include "../Logger.h"
#include "../utility/DetectorControls.h"
#include "../utility/SystemJournal.h"
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

EventSource::EventSource(const ILogger& logger, std::shared_ptr<SystemJournal> journal,
												 std::shared_ptr<DetectorControls> controls)
		: logger_(logger), journal_(std::move(journal)), controls_(std::move(controls)),
			work_(boost::asio::make_work_guard(io_context_)), worker_([this]() { io_context_.run(); })
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

bool EventSource::HasDetector(const std::string& id)
{
	return Invoke([&]() {
		return controls_ && std::any_of(generators_.begin(), generators_.end(), [&](const auto& generator) {
						 return controls_->IdForTopic(generator->Topic()) == id;
					 });
	});
}
void EventSource::ConfigureDetector(const std::string& id, int interval, bool automatic, bool enabled)
{
	Invoke([&]() {
		if (!HasDetector(id))
			throw std::invalid_argument("Detector unavailable");
		if (interval < 1)
			throw std::invalid_argument("Invalid event interval");
		controls_->Set(id, enabled);
		controls_->SetAutomatic(id, automatic);
		for (const auto& generator : generators_)
			if (controls_->IdForTopic(generator->Topic()) == id)
				// Line sampling also feeds counting, field rules and scene metadata.
				generator->Configure(interval, automatic || id == "tt:LineDetector");
	});
}
void EventSource::GenerateOnce(const std::string& id, const std::string& configuration, const std::string& rule)
{
	Invoke([&]() {
		if (!running_ || !HasDetector(id))
			throw std::runtime_error("Detector unavailable");
		if (!controls_->Enabled(id) || (id.starts_with("tt:") && !controls_->Enabled("rules")) ||
				(!configuration.empty() && !controls_->RuleEnabled(configuration, rule)))
			throw std::runtime_error("Detector disabled");
		manual_ = true;
		manual_id_ = id;
		manual_configuration_ = configuration;
		manual_rule_ = rule;
		try
		{
			for (const auto& generator : generators_)
				if (controls_->IdForTopic(generator->Topic()) == id)
					generator->GenerateOnce();
		}
		catch (...)
		{
			manual_ = false;
			throw;
		}
		manual_ = false;
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

void EventSource::SetNamespaces(std::map<std::string, std::string> namespaces)
{
	Invoke([this, namespaces = std::move(namespaces)]() {
		if (running_)
			throw std::logic_error("Configure event namespaces before EventSource::Run");
		namespaces_ = namespaces;
	});
}

std::map<std::string, std::string> EventSource::Namespaces()
{
	return Invoke([this]() { return namespaces_; });
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
				if (message.property_operation != "Initialized" || (controls_ && !controls_->Allows(message)))
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
	if (controls_ && !controls_->Allows(message))
		return;
	if (controls_)
	{
		const auto id = controls_->IdForTopic(message.topic);
		if (!manual_ && !controls_->Automatic(id))
			return;
		if (manual_)
		{
			if (id != manual_id_)
				return;
			if (!manual_configuration_.empty())
			{
				if (message.analytics_configuration != manual_configuration_)
					return;
				const auto matches = std::any_of(
						message.source_item_descriptions.begin(), message.source_item_descriptions.end(), [&](const auto& item) {
							return (item.first == "Rule" || item.first == "RuleName") && item.second == manual_rule_;
						});
				if (!matches)
					return;
			}
		}
	}
	if (journal_)
		journal_->Detector(message);
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
