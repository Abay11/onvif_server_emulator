#include "../onvif/Notification.h"
#include "../utility/SystemJournal.h"
#include <boost/test/unit_test.hpp>
#include <thread>

BOOST_AUTO_TEST_SUITE(SystemJournal)
BOOST_AUTO_TEST_CASE(retention_cursors_filters_and_reset_are_explicit)
{
	osrv::SystemJournal journal(3);
	journal.Add("info", "system", "application", "Start");
	journal.Add("info", "detector", "Motion", "Front door");
	journal.Add("warn", "client", "ONVIF", "Rejected");
	journal.Add("error", "system", "application", "Failed");
	auto page = journal.Read(0, 1, {"detector", "", "FRONT"});
	BOOST_REQUIRE_EQUAL(page.entries.size(), 1u);
	BOOST_TEST(page.entries.front().id == 2u);
	BOOST_TEST(page.dropped == 1u);
	BOOST_TEST(page.more);
	auto rest = journal.Read(page.cursor, 10, {"detector", "", "FRONT"});
	BOOST_TEST(rest.entries.empty());
	BOOST_TEST(rest.cursor == 4u);
	BOOST_TEST(!rest.more);
	BOOST_TEST(journal.Read(0, 10, {}).entries.size() == 3u);
	BOOST_TEST(journal.Read(999, 10, {}).reset);
	BOOST_TEST(journal.Read(0, 10, {"", "error", ""}).entries.size() == 1u);
	BOOST_CHECK_THROW(journal.Read(0, 0, {}), std::invalid_argument);
}
BOOST_AUTO_TEST_CASE(concurrent_publishers_produce_one_ordered_history_for_independent_readers)
{
	osrv::SystemJournal journal(1024);
	std::vector<std::thread> workers;
	for (unsigned thread = 0; thread < 4; ++thread)
		workers.emplace_back([&journal] {
			for (unsigned i = 0; i < 200; ++i)
				journal.Add("info", "system", "test", "entry");
		});
	for (auto& worker : workers)
		worker.join();
	std::uint64_t cursor = 0;
	for (unsigned page = 0; page < 4; ++page)
	{
		auto batch = journal.Read(cursor, 200, {});
		BOOST_REQUIRE_EQUAL(batch.entries.size(), 200u);
		for (const auto& entry : batch.entries)
			BOOST_TEST(entry.id == ++cursor);
		BOOST_TEST(batch.dropped == 0u);
	}
	BOOST_TEST(cursor == 800u);
	BOOST_TEST(journal.Read(0, 200, {}).entries.front().id == 1u);
}
BOOST_AUTO_TEST_CASE(summaries_are_bounded_and_exclude_complex_payloads_and_identity_data)
{
	osrv::SystemJournal journal;
	osrv::event::NotificationMessage notification;
	notification.topic = "tns1:RuleEngine/FaceRecognition";
	notification.source_item_descriptions = {{"Rule", "Entry"}};
	notification.data_name = "Likelihood";
	notification.data_value = "0.99";
	notification.data_simple_items = {{"ImageUri", "secret image URI"}, {"EnrollmentID", "secret identity"}};
	boost::property_tree::ptree image;
	image.put("Image", std::string(100000, 'x'));
	notification.data_element_items.push_back(image);
	journal.Detector(notification);
	const auto entry = journal.Read(0, 100, {}).entries.front();
	BOOST_TEST(entry.message.find("Likelihood=0.99") != std::string::npos);
	BOOST_TEST(entry.message.find("Rule=Entry") != std::string::npos);
	BOOST_TEST(entry.message.find("secret") == std::string::npos);
	BOOST_TEST(entry.message.size() < 1024u);
	journal.Add("info", "system", "test", std::string(1023, 'a') + "Ж");
	BOOST_TEST(journal.Read(1, 100, {}).entries.front().message.size() == 1023u);
	journal.Client("ONVIF", "127.0.0.1", "user");
	journal.Client("ONVIF", "127.0.0.1", "user");
	BOOST_TEST(journal.Read(2, 100, {}).entries.size() == 1u);
}
BOOST_AUTO_TEST_SUITE_END()
