#include "../utility/AnalyticsRules.h"
#include <boost/property_tree/json_parser.hpp>
#include <boost/test/unit_test.hpp>
#include <limits>

namespace
{
using Tree = boost::property_tree::ptree;
using Detector = utility::AnalyticsRules;
using namespace std::chrono_literals;
std::string simple(const std::string& name, const std::string& value)
{
	return "<tt:SimpleItem Name='" + name + "' Value='" + value + "'/>";
}
std::string segment(double x)
{
	return "<tt:ElementItem Name='Segments'><tt:Polyline><tt:Point x='" + std::to_string(x) + "' y='-1'/><tt:Point x='" +
				 std::to_string(x) + "' y='1'/></tt:Polyline></tt:ElementItem>";
}
std::string rule(const std::string& params = "", const std::string& name = "counter")
{
	return "<tan:Rule Name='" + name + "' Type='tt:LineCounting'><tt:Parameters>" + params +
				 "</tt:Parameters></tan:Rule>";
}
std::string request(const std::string& op, const std::string& body, const std::string& type = "")
{
	return "<s:Envelope xmlns:s='http://www.w3.org/2003/05/soap-envelope' "
				 "xmlns:tan='http://www.onvif.org/ver20/analytics/wsdl' xmlns:tt='http://www.onvif.org/ver10/schema'>"
				 "<s:Body><tan:" +
				 op + ">" + (type.empty() ? "" : "<tan:RuleType>" + type + "</tan:RuleType>") +
				 "<tan:ConfigurationToken>AnalyticsConfig_0</tan:ConfigurationToken>" + body + "</tan:" + op +
				 "></s:Body></s:Envelope>";
}
struct Fixture
{
	static Tree Load()
	{
		Tree p;
		boost::property_tree::read_json("../../server_configs/media_profiles.config", p);
		return p;
	}
	Tree profiles = Load();
	utility::counting::Time now = std::chrono::sys_days(std::chrono::year(2026) / 1 / 1) + 12h;
	bool failSave = false;
	unsigned saves = 0;
	Detector detector{profiles,
										[this](const std::string&, const Tree& rules) {
											if (failSave)
												throw std::runtime_error("save failed");
											profiles.get_child("Analytics").front().second.put_child("rules", rules);
											++saves;
										},
										[this] { return now; }};
	Tree call(const std::string& op, const std::string& body = "", const std::string& type = "")
	{
		return detector.Execute(op, request(op, body, type));
	}
	void create(const std::string& params = "")
	{
		call("CreateRules", rule(params));
		auto events = detector.TickCounting();
		BOOST_REQUIRE_EQUAL(events.size(), 1u);
		BOOST_TEST(events.front().property_operation == "Initialized");
		BOOST_TEST(events.front().data_value == "0");
	}
	auto sample(double x, const std::string& id = "1")
	{
		return detector.SampleObject("VideoSource0", id, x, 0);
	}
	int count()
	{
		return std::stoi(detector.SynchronizeCounts().front().data_value);
	}
};
} // namespace

BOOST_FIXTURE_TEST_SUITE(line_counting, Fixture)
BOOST_AUTO_TEST_CASE(discovery_defaults_persistence_and_lifecycle)
{
	auto descriptions = call("GetSupportedRules").get_child("tan:SupportedRules");
	BOOST_TEST(descriptions.count("tt:RuleDescription") == 6u);
	auto desc = std::next(descriptions.begin(), 3)->second;
	BOOST_TEST(desc.get<std::string>("<xmlattr>.Name") == "tt:LineCounting");
	BOOST_TEST(desc.get<bool>("tt:Messages.<xmlattr>.IsProperty"));
	BOOST_TEST(desc.get<std::string>("tt:Messages.tt:ParentTopic") == utility::counting::Topic);
	BOOST_TEST(desc.get<std::string>("tt:Messages.tt:Data.tt:SimpleItemDescription.<xmlattr>.Type") == "xs:int");
	auto options = call("GetRuleOptions", "", "tt:LineCounting");
	BOOST_TEST(options.count("tan:RuleOptions") == 3u);
	BOOST_TEST(options.back().second.get<std::string>("tt:DurationRange.tt:Max") == "P1D");
	create();
	BOOST_TEST(sample(-0.5).empty());
	auto changed = sample(0.5);
	BOOST_REQUIRE_EQUAL(changed.size(), 1u);
	BOOST_TEST(changed.front().topic == utility::counting::Topic);
	BOOST_TEST(changed.front().property_operation == "Changed");
	BOOST_TEST(changed.front().data_name == "Count");
	BOOST_TEST(changed.front().data_value == "1");
	BOOST_TEST(changed.front().source_item_descriptions.back().second == "counter");
	BOOST_TEST(detector.SynchronizeCounts().front().property_operation == "Initialized");
	BOOST_TEST(detector.Synchronize().empty());
	BOOST_TEST(sample(0.5).empty());
	Detector restored(profiles, [](const auto&, const auto&) {}, [this] { return now; });
	BOOST_TEST(restored.Execute("GetRules", request("GetRules", "")).count("tan:Rule") == 1u);
	BOOST_TEST(restored.SynchronizeCounts().front().data_value == "0");
	call("DeleteRules", "<tan:RuleName>counter</tan:RuleName>");
	auto deleted = detector.TickCounting();
	BOOST_REQUIRE_EQUAL(deleted.size(), 1u);
	BOOST_TEST(deleted.front().property_operation == "Deleted");
	BOOST_TEST(deleted.front().data_value == "1");
	BOOST_TEST(detector.SynchronizeCounts().empty());
}
BOOST_AUTO_TEST_CASE(each_polyline_counts_and_direction_filters)
{
	create(simple("Direction", "Right") + segment(-0.25) + segment(0.25));
	sample(-0.75);
	BOOST_TEST(sample(0.75).front().data_value == "2");
	BOOST_TEST(sample(-0.75).empty());
	BOOST_TEST(count() == 2);
	call("ModifyRules", rule(simple("Direction", "Left")));
	detector.TickCounting();
	BOOST_TEST(count() == 2); // configuration changes retain the count, but clear trajectory history
	sample(0.75);
	BOOST_TEST(sample(-0.75).front().data_value == "4");
}
BOOST_AUTO_TEST_CASE(pass_all_tracks_each_object_and_rearms)
{
	create(simple("PassAllPolylines", "true") + segment(-0.25) + segment(0.25));
	sample(-0.75);
	BOOST_TEST(sample(0).empty());
	sample(0, "2");
	BOOST_TEST(sample(0.75, "2").empty()); // another object cannot complete object 1's passage
	BOOST_TEST(count() == 0);
	BOOST_TEST(sample(0.75).front().data_value == "1");
	BOOST_TEST(sample(0).empty());
	BOOST_TEST(sample(-0.75).front().data_value == "2");
	detector.ForgetObject("VideoSource0", "1");
	BOOST_TEST(sample(0.75).empty());
	BOOST_TEST(count() == 2);
}
BOOST_AUTO_TEST_CASE(report_interval_flushes_without_more_objects_and_sync_is_current)
{
	create(simple("ReportTimeInterval", "PT5S"));
	sample(-0.5);
	now += 1s;
	BOOST_TEST(sample(0.5).empty());
	BOOST_TEST(count() == 1);
	now += 3s;
	BOOST_TEST(detector.TickCounting().empty());
	now += 1s;
	auto report = detector.TickCounting();
	BOOST_REQUIRE_EQUAL(report.size(), 1u);
	BOOST_TEST(report.front().data_value == "1");
	now += 5s;
	BOOST_TEST(detector.TickCounting().empty()); // no duplicate unchanged property events
}
BOOST_AUTO_TEST_CASE(shared_detector_type_keeps_independent_channel_counts_and_report_intervals)
{
	create(simple("ReportTimeInterval", "PT5S"));
	auto otherConfig = profiles.get_child("Analytics").front().second;
	otherConfig.put("token", "other-count");
	profiles.get_child("Analytics").push_back({"", otherConfig});
	auto otherSource = profiles.get_child("VideoSource").front().second;
	otherSource.put("token", "other-source-config");
	otherSource.put("SourceToken", "other-source");
	otherSource.get_child("CompatibleAnalyticsConfigurations").front().second.put_value("other-count");
	profiles.get_child("VideoSource").push_back({"", otherSource});
	Detector isolated(profiles, [](const auto&, const auto&) {}, [this] { return now; });

	isolated.SampleObject("VideoSource0", "1", -0.5, 0);
	isolated.SampleObject("other-source", "1", 0.5, 0);
	now += 1s;
	BOOST_TEST(isolated.SampleObject("VideoSource0", "1", 0.5, 0).empty());
	now += 4s;
	BOOST_TEST(isolated.TickCounting("other-source").empty());
	const auto first = isolated.TickCounting("VideoSource0");
	BOOST_REQUIRE_EQUAL(first.size(), 1u);
	BOOST_TEST(first.front().data_value == "1");
	const auto second = isolated.SampleObject("other-source", "1", -0.5, 0);
	BOOST_REQUIRE_EQUAL(second.size(), 1u);
	BOOST_TEST(second.front().source_item_descriptions.front().second == "other-source");
	BOOST_TEST(second.front().data_value == "1");
}
BOOST_AUTO_TEST_CASE(reset_times_clear_counts_and_incomplete_passages)
{
	create(simple("PassAllPolylines", "true") + simple("ReportTimeInterval", "PT30S") + simple("ResetTime", "12:00:05Z") +
				 simple("ResetTime", "12:00:10Z") + segment(-0.25) + segment(0.25));
	sample(-0.75);
	sample(0.75);
	BOOST_TEST(count() == 1);
	now += 5s;
	auto reset = detector.TickCounting();
	BOOST_REQUIRE_EQUAL(reset.size(), 1u);
	BOOST_TEST(reset.front().data_value == "0");
	sample(-0.75);
	sample(0); // one of two polylines
	now += 5s;
	BOOST_TEST(detector.TickCounting().empty()); // even a zero count resets incomplete passages
	sample(0);
	sample(0.75);
	BOOST_TEST(count() == 0);
}
BOOST_AUTO_TEST_CASE(crossing_on_a_report_boundary_emits_current_count_once)
{
	create(simple("ReportTimeInterval", "PT5S"));
	sample(-0.5);
	now += 1s;
	sample(0.5);
	now += 4s;
	auto report = sample(-0.5);
	BOOST_REQUIRE_EQUAL(report.size(), 1u);
	BOOST_TEST(report.front().data_value == "2");
	now += 5s;
	BOOST_TEST(detector.TickCounting().empty());
}
BOOST_AUTO_TEST_CASE(local_midnight_timezone_offsets_and_skipped_days)
{
	std::tm date{};
	date.tm_year = 126;
	date.tm_mon = 0;
	date.tm_mday = 1;
	date.tm_hour = 23;
	date.tm_min = 59;
	date.tm_sec = 58;
	date.tm_isdst = -1;
	now = std::chrono::system_clock::from_time_t(std::mktime(&date));
	create(simple("ResetTime", "24:00:00"));
	sample(-0.5);
	sample(0.5);
	now += 3s;
	BOOST_TEST(detector.TickCounting().front().data_value == "0");
	call("ModifyRules", rule(simple("ResetTime", "15:00:05+03:00")));
	detector.TickCounting();
	now = std::chrono::sys_days(std::chrono::year(2026) / 1 / 2) + 12h;
	detector.TickCounting();
	sample(-0.5);
	sample(0.5);
	now += 5s;
	BOOST_TEST(detector.TickCounting().front().data_value == "0");
	sample(-0.5);
	sample(0.5);
	now += 72h;
	BOOST_TEST(detector.TickCounting().front().data_value == "0");
}
BOOST_AUTO_TEST_CASE(validation_atomic_failures_and_save_rollback)
{
	for (auto params :
			 {simple("ReportTimeInterval", "P"), simple("ReportTimeInterval", "P1DT"), simple("ReportTimeInterval", "-PT1S"),
				simple("ReportTimeInterval", "P1M"), simple("ReportTimeInterval", "PT86401S"), simple("ResetTime", "24:00:01"),
				simple("ResetTime", "12:60:00"), simple("ResetTime", "12:00:00+14:01"), simple("PassAllPolylines", "yes"),
				simple("Direction", "Up"), simple("Direction", "Any") + simple("Direction", "Left"),
				segment(0) + simple("Direction", "Left"),
				segment(0) + segment(0.1) + segment(0.2) + segment(0.3) + segment(0.4)})
		BOOST_CHECK_THROW(call("CreateRules", rule(params)), utility::RuleFault);
	BOOST_TEST(saves == 0u);
	create(simple("ReportTimeInterval", "PT0.000000S"));
	sample(-0.5);
	BOOST_CHECK_THROW(call("ModifyRules", rule(simple("Direction", "Left")) + rule("", "missing")), utility::RuleFault);
	failSave = true;
	BOOST_CHECK_THROW(call("ModifyRules", rule(simple("Direction", "Left"))), utility::RuleFault);
	BOOST_TEST(sample(0.5).front().data_value == "1");
	BOOST_TEST(detector.TickCounting().empty());
}
BOOST_AUTO_TEST_CASE(saturation_respects_xs_int)
{
	create();
	const auto& cfg = profiles.get_child("Analytics").front().second.get_child("rules").front().second;
	utility::counting::State state(now, std::numeric_limits<int>::max() - 1);
	state.Sample(cfg, "1", {-0.5, 0}, now);
	BOOST_TEST(state.Sample(cfg, "1", {0.5, 0}, now));
	BOOST_TEST(state.count == std::numeric_limits<int>::max());
	BOOST_TEST(!state.Sample(cfg, "1", {-0.5, 0}, now));
}
BOOST_AUTO_TEST_SUITE_END()
