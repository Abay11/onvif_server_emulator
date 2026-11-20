#include "../onvif_services/EventSource.h"
#include "../utility/SystemJournal.h"
#include "../onvif_services/pullpoint/event_generators.h"
#include "../onvif_services/pullpoint/pull_point.h"

#include <boost/property_tree/xml_parser.hpp>
#include <boost/test/unit_test.hpp>

#include <fstream>
#include <sstream>

namespace
{
using namespace osrv::event;
namespace pt = boost::property_tree;

class QuietLogger : public ILogger
{
public:
	QuietLogger() : ILogger(LVL_ERR)
	{
	}
	void Error(const std::string&) const override
	{
	}
	void Warn(const std::string&) const override
	{
	}
	void Info(const std::string&) const override
	{
	}
	void Debug(const std::string&) const override
	{
	}
	void Trace(const std::string&) const override
	{
	}
};

NotificationMessage Message(const std::string& value = "true", bool property = true)
{
	NotificationMessage message;
	message.topic = "tns1:Device/Trigger/DigitalInput";
	message.utc_time = "2026-11-19T10:11:12.123Z";
	message.property_operation = property ? "Changed" : "";
	message.source_item_descriptions = {{"InputToken", "input&\"<0>"}};
	message.data_name = "LogicalState";
	message.data_value = value;
	return message;
}

class Generator : public IEventGenerator
{
public:
	Generator(boost::asio::io_context& context, const ILogger& logger, bool property = true)
			: IEventGenerator(3600, "test", context, logger), property_(property)
	{
	}
	void Emit(NotificationMessage message)
	{
		event_signal_(std::move(message));
	}
	std::deque<NotificationMessage> GenerateSynchronizationEvent() const override
	{
		if (!property_)
			return {};
		auto message = Message();
		message.property_operation = "Initialized";
		return {message};
	}
	auto Deadline() const
	{
		return alarm_timer_.expiry();
	}

private:
	bool property_;
};

struct Fixture
{
	QuietLogger logger;
	std::shared_ptr<osrv::SystemJournal> journal = std::make_shared<osrv::SystemJournal>();
	std::shared_ptr<EventSource> source = std::make_shared<EventSource>(logger, journal);
	std::shared_ptr<Generator> property = std::make_shared<Generator>(source->GetIoContext(), logger);
	std::shared_ptr<Generator> transient = std::make_shared<Generator>(source->GetIoContext(), logger, false);
	Fixture()
	{
		source->AddGenerator(property);
		source->AddGenerator(transient);
		source->Run();
	}
	void Emit(NotificationMessage message)
	{
		source->Invoke([this, message = std::move(message)]() { property->Emit(message); });
	}
	std::deque<NotificationMessage> Pull(const std::shared_ptr<PullPoint>& point, std::size_t limit = 50)
	{
		std::promise<std::deque<NotificationMessage>> promise;
		auto result = promise.get_future();
		source->Invoke([&]() {
			point->PullMessages([&](const auto&, auto&& messages, const auto&) { promise.set_value(std::move(messages)); },
													nullptr, 0, limit);
		});
		return result.get();
	}
};
} // namespace

BOOST_AUTO_TEST_SUITE(SharedEvents)

BOOST_FIXTURE_TEST_CASE(independent_pullpoints_and_metadata_receive_the_same_events, Fixture)
{
	NotificationsManager manager(logger, {}, source);
	auto metadata = source->Subscribe({}, false);
	auto deadline = source->Invoke([this]() { return property->Deadline(); });
	auto first = manager.CreatePullPoint();
	auto second = manager.CreatePullPoint();
	BOOST_TEST(first->GetSubscriptionReference() != second->GetSubscriptionReference());
	BOOST_TEST(Pull(first).front().property_operation == "Initialized");
	BOOST_TEST(Pull(second).front().property_operation == "Initialized");
	source->Run();
	BOOST_CHECK(deadline == source->Invoke([this]() { return property->Deadline(); }));
	auto expected = Message("same-event");
	Emit(expected);
	BOOST_CHECK(serialize_notification(Pull(first).front()) == serialize_notification(expected));
	BOOST_CHECK(serialize_notification(Pull(second).front()) == serialize_notification(expected));
	auto received = metadata->Take(10);
	BOOST_REQUIRE_EQUAL(received.messages.size(), 1);
	BOOST_CHECK(serialize_notification(*received.messages.front()) == serialize_notification(expected));
	manager.Unsubscribe(first->GetSubscriptionReference());
	BOOST_CHECK_THROW(manager.SetSynchronizationPoint(first->GetSubscriptionReference()), std::runtime_error);
	Emit(Message("next"));
	BOOST_TEST(Pull(second).front().data_value == "next");
	BOOST_TEST(metadata->Take(10).messages.front()->data_value == "next");
	BOOST_CHECK(deadline == source->Invoke([this]() { return property->Deadline(); }));
}

BOOST_FIXTURE_TEST_CASE(synchronization_preserves_transients_and_message_limit_leaves_remainder, Fixture)
{
	NotificationsManager manager(logger, {}, source);
	auto point = manager.CreatePullPoint();
	Pull(point);
	Emit(Message("passage", false));
	manager.SetSynchronizationPoint(point->GetSubscriptionReference());
	auto first = Pull(point, 1);
	BOOST_REQUIRE_EQUAL(first.size(), 1);
	BOOST_TEST(first.front().data_value == "passage");
	BOOST_TEST(first.front().property_operation.empty());
	auto snapshot = Pull(point, 1);
	BOOST_REQUIRE_EQUAL(snapshot.size(), 1);
	BOOST_TEST(snapshot.front().property_operation == "Initialized");
	BOOST_TEST(Pull(point).empty());
}

BOOST_FIXTURE_TEST_CASE(slow_reader_is_bounded_and_loss_is_recoverable_without_affecting_fast_reader, Fixture)
{
	auto slow = source->Subscribe({2, 100000}, false);
	auto fast = source->Subscribe({}, false);
	for (int index = 0; index < 4; ++index)
	{
		Emit(Message(std::to_string(index)));
		auto received = fast->Take(10);
		BOOST_REQUIRE_EQUAL(received.messages.size(), 1);
		BOOST_TEST(received.messages.front()->data_value == std::to_string(index));
		BOOST_TEST(received.dropped == 0);
	}
	auto batch = slow->Take(10);
	BOOST_TEST(batch.dropped == 2);
	BOOST_TEST(batch.needs_synchronization);
	BOOST_REQUIRE_EQUAL(batch.messages.size(), 2);
	BOOST_TEST(batch.messages.front()->data_value == "2");
	source->Synchronize(slow);
	auto snapshot = slow->Take(10);
	BOOST_TEST(!snapshot.needs_synchronization);
	BOOST_REQUIRE_EQUAL(snapshot.messages.size(), 1);
	BOOST_TEST(snapshot.messages.front()->property_operation == "Initialized");
}

BOOST_FIXTURE_TEST_CASE(byte_limits_oversized_events_and_incomplete_snapshots_report_loss, Fixture)
{
	auto size = notification_size(Message());
	auto queue = source->Subscribe({10, size * 2}, false);
	Emit(Message(std::string(size * 3, 'x')));
	auto oversized = queue->Take(10);
	BOOST_TEST(oversized.messages.empty());
	BOOST_TEST(oversized.dropped == 1);
	BOOST_TEST(oversized.needs_synchronization);
	Emit(Message());
	Emit(Message());
	Emit(Message());
	auto batch = queue->Take(10);
	BOOST_TEST(batch.messages.size() == 2);
	BOOST_TEST(batch.dropped == 1);
	auto tiny = source->Subscribe({1, 1});
	BOOST_TEST(tiny->NeedsSynchronization());
	BOOST_TEST(tiny->Take(1).dropped == 1);
}

BOOST_FIXTURE_TEST_CASE(consumers_share_immutable_messages_and_wait_on_their_own_executor, Fixture)
{
	auto first = source->Subscribe({}, false);
	auto second = source->Subscribe({}, false);
	boost::asio::io_context consumer;
	int calls = 0;
	first->AsyncWait(consumer, [&]() { ++calls; });
	Emit(Message());
	Emit(Message());
	BOOST_TEST(calls == 0);
	consumer.poll();
	BOOST_TEST(calls == 1);
	BOOST_CHECK(first->Take(1).messages.front() == second->Take(1).messages.front());
	consumer.restart();
	first->AsyncWait(consumer, [&]() { ++calls; });
	first->CancelWait();
	consumer.poll();
	BOOST_TEST(calls == 1);
	consumer.restart();
	first->AsyncWait(consumer, [&]() { ++calls; });
	first->Close();
	consumer.poll();
	BOOST_TEST(calls == 1);
	Emit(Message());
	BOOST_TEST(first->Take(10).messages.empty());
	BOOST_TEST(!second->Take(10).messages.empty());
}

BOOST_AUTO_TEST_CASE(subscription_references_match_complete_paths)
{
	BOOST_TEST(!compare_subscription_references("http://host/onvif/event_service/s10", "onvif/event_service/s1"));
	BOOST_TEST(!compare_subscription_references("http://host/onvif/event_service/s1/extra", "onvif/event_service/s1"));
	BOOST_TEST(!compare_subscription_references("http://host/xonvif/event_service/s1", "onvif/event_service/s1"));
	BOOST_TEST(compare_subscription_references("/onvif/event_service/s1", "onvif/event_service/s1"));
}

BOOST_AUTO_TEST_CASE(event_source_supplies_custom_namespace_bindings_to_transport_adapters)
{
	QuietLogger logger;
	auto source = std::make_shared<EventSource>(logger);
	source->SetNamespaces({{"vendor", "urn:osrv:metadata-test"}});
	auto message = Message();
	message.topic = "vendor:Device/Trigger/DigitalInput";
	auto xml = serialize_metadata({message}, source->Namespaces());
	BOOST_TEST(xml.find("xmlns:vendor=\"urn:osrv:metadata-test\"") != std::string::npos);
	source->Run();
	BOOST_CHECK_THROW(source->SetNamespaces({}), std::logic_error);
}

BOOST_FIXTURE_TEST_CASE(pending_pull_is_not_overwritten_and_closing_cancels_queued_wakes, Fixture)
{
	NotificationsManager manager(logger, {}, source);
	auto point = manager.CreatePullPoint();
	Pull(point);
	std::promise<void> completed;
	auto result = completed.get_future();
	source->Invoke([&]() {
		point->PullMessages([&](const auto&, auto&&, const auto&) { completed.set_value(); }, nullptr);
		BOOST_CHECK_THROW(point->PullMessages({}, nullptr), SubscriptionFault);
		point->Close();
	});
	result.get();
	Emit(Message());
	BOOST_TEST(source->Invoke([&]() { return point->IsClosed(); }));
}

BOOST_FIXTURE_TEST_CASE(subscription_expiration_is_independent_and_source_shutdown_closes_queues, Fixture)
{
	NotificationsManager manager(logger, {}, source);
	auto first = manager.CreatePullPoint();
	auto second = manager.CreatePullPoint();
	std::promise<void> expired;
	auto result = expired.get_future();
	source->Invoke([&]() {
		first->Renew(
				[&]() {
					first->Close();
					expired.set_value();
				},
				std::chrono::seconds(0));
	});
	result.get();
	BOOST_TEST(source->Invoke([&]() { return first->IsClosed(); }));
	BOOST_TEST(!source->Invoke([&]() { return second->IsClosed(); }));
	std::shared_ptr<EventSubscription> queue;
	{
		auto temporary = std::make_shared<EventSource>(logger);
		queue = temporary->Subscribe({}, false);
	}
	boost::asio::io_context receiver;
	bool called = false;
	queue->AsyncWait(receiver, [&]() { called = true; });
	receiver.poll();
	BOOST_TEST(!called);
}

BOOST_AUTO_TEST_CASE(subscription_faults_have_typed_details)
{
	for (const auto* name : {"wsrf-r:ResourceUnknownFault", "wsnt:UnableToGetMessagesFault"})
	{
		auto xml = serialize_subscription_fault({name, "Unavailable"});
		std::istringstream input(xml);
		pt::ptree parsed;
		pt::read_xml(input, parsed);
		BOOST_TEST(
				!parsed.get<std::string>(std::string("s:Envelope.s:Body.s:Fault.s:Detail.") + name + ".wsrf-bf:Timestamp")
						 .empty());
		std::ofstream(std::string(name).substr(std::string(name).find(':') + 1) + ".xml") << xml;
	}
}

BOOST_AUTO_TEST_CASE(notification_serialization_is_shared_escaped_and_preserves_structured_items)
{
	auto message = Message("A&B<\"", false);
	message.data_name.clear();
	message.data_simple_items = {{"Confidence", "0.9"}, {"Label", "A&B<\""}};
	pt::ptree item;
	item.put("<xmlattr>.Name", "Recognition");
	item.put("ext:Recognition.<xmlattr>.xmlns:ext", "urn:osrv:test");
	item.put("ext:Recognition.ext:Text", "Plate <123> & Face");
	message.data_element_items.push_back(item);
	auto soap = serialize_notification_messages({message}, "subscription");
	auto xml = serialize_metadata({message});
	std::istringstream input(xml);
	pt::ptree parsed;
	pt::read_xml(input, parsed);
	const auto& node = parsed.get_child("tt:MetadataStream.tt:Event.wsnt:NotificationMessage");
	BOOST_CHECK(node == soap.get_child("wsnt:NotificationMessage"));
	BOOST_TEST(node.get<std::string>("wsnt:Message.tt:Message.tt:Source.tt:SimpleItem.<xmlattr>.Value") == "input&\"<0>");
	const auto& data = node.get_child("wsnt:Message.tt:Message.tt:Data");
	BOOST_TEST(data.count("tt:SimpleItem") == 2);
	BOOST_TEST(data.get<std::string>("tt:ElementItem.ext:Recognition.ext:Text") == "Plate <123> & Face");
	BOOST_TEST(!node.get_optional<std::string>("wsnt:Message.tt:Message.<xmlattr>.PropertyOperation"));
	BOOST_TEST(xml.find("Envelope") == std::string::npos);
	// CTest's working directory is a build fixture. The pinned schema validator
	// consumes this independently generated artifact in the integration check.
	std::ofstream("stage2_metadata.xml") << xml;
	std::ofstream("stage2_metadata_empty.xml") << serialize_metadata({});
}

BOOST_FIXTURE_TEST_CASE(journal_receives_events_without_clients_and_survives_slow_consumer_overflow, Fixture)
{
	Emit(Message("before any clients"));
	auto slow = source->Subscribe({1, 4096}, false);
	for (unsigned index = 0; index < 10; ++index) Emit(Message(std::to_string(index)));
	auto batch = journal->Read(0, 100, {});
	BOOST_REQUIRE_EQUAL(batch.entries.size(), 11u);
	BOOST_TEST(batch.dropped == 0u);
	BOOST_TEST(batch.entries.front().message.find("before any clients") != std::string::npos);
	BOOST_TEST(slow->Take(100).dropped == 9u);
	// Reading either transport queue or journal cannot consume another reader's data.
	BOOST_TEST(journal->Read(0, 100, {}).entries.size() == 11u);
	BOOST_TEST(journal->Read(batch.cursor, 100, {}).entries.empty());
}

BOOST_AUTO_TEST_SUITE_END()
