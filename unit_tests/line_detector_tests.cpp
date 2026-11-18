#include "../utility/AnalyticsRules.h"
#include "../utility/EventService.h"
#include "../utility/LineDetector.h"
#include <boost/property_tree/json_parser.hpp>
#include <boost/test/unit_test.hpp>
#include <limits>

namespace
{
using Tree = boost::property_tree::ptree;
using Detector = utility::AnalyticsRules;
std::string request(const std::string& op, const std::string& body = "", const std::string& type = "")
{
	return "<s:Envelope xmlns:s='http://www.w3.org/2003/05/soap-envelope' "
				 "xmlns:tan='http://www.onvif.org/ver20/analytics/wsdl' xmlns:tt='http://www.onvif.org/ver10/schema'>"
				 "<s:Body><tan:" +
				 op + ">" + (type.empty() ? "" : "<tan:RuleType>" + type + "</tan:RuleType>") +
				 "<tan:ConfigurationToken>AnalyticsConfig_0</tan:ConfigurationToken>" + body + "</tan:" + op +
				 "></s:Body></s:Envelope>";
}
std::string rule(const std::string& name, const std::string& params = "", const std::string& type = "tt:LineDetector")
{
	return "<tan:Rule Name='" + name + "' Type='" + type + "'><tt:Parameters>" + params + "</tt:Parameters></tan:Rule>";
}
std::string segments(const std::string& points)
{
	return "<tt:ElementItem Name='Segments'><tt:Polyline>" + points + "</tt:Polyline></tt:ElementItem>";
}
std::string direction(const std::string& value)
{
	return "<tt:SimpleItem Name='Direction' Value='" + value + "'/>";
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
	unsigned saves = 0;
	Detector detector{profiles, [this](const std::string&, const Tree& rules) {
											profiles.get_child("Analytics").front().second.put_child("rules", rules);
											++saves;
										}};
	Tree call(const std::string& op, const std::string& body = "", const std::string& type = "")
	{
		return detector.Execute(op, request(op, body, type));
	}
	std::deque<Detector::Message> sample(double x, double y = 0, const std::string& id = "7")
	{
		return detector.SampleObject("VideoSource0", id, x, y);
	}
};
} // namespace

BOOST_FIXTURE_TEST_SUITE(line_detectors, Fixture)
BOOST_AUTO_TEST_CASE(discovery_options_defaults_and_reload)
{
	auto supported = call("GetSupportedRules").get_child("tan:SupportedRules");
	BOOST_TEST(supported.count("tt:RuleDescription") == 2u);
	const auto& desc = supported.back().second;
	BOOST_TEST(desc.get<std::string>("<xmlattr>.Name") == "tt:LineDetector");
	BOOST_TEST(!desc.get<bool>("tt:Messages.<xmlattr>.IsProperty"));
	BOOST_TEST(desc.get<std::string>("tt:Messages.tt:ParentTopic") == utility::line::Topic);
	auto options = call("GetRuleOptions", "", "tt:LineDetector");
	BOOST_TEST(options.count("tan:RuleOptions") == 2u);
	BOOST_TEST(options.front().second.get<std::string>("tt:StringList") == "Left Right Any");
	BOOST_TEST(options.back().second.get<int>("tt:IntRange.tt:Min") == 2);
	BOOST_TEST(options.back().second.get<int>("tt:IntRange.tt:Max") == 8);
	BOOST_TEST(call("GetRuleOptions").count("tan:RuleOptions") == 3u);
	call("CreateRules", rule("entrance"));
	auto xml = call("GetRules").get_child("tan:Rule");
	BOOST_TEST(xml.get<std::string>("tt:Parameters.tt:SimpleItem.<xmlattr>.Value") == "Any");
	BOOST_TEST(xml.get_child("tt:Parameters.tt:ElementItem.tt:Polyline").size() == 2u);
	BOOST_TEST(detector.Synchronize().empty());
	BOOST_TEST(detector.Sample(100, 100, 1).empty());
	Detector restored(profiles, [](const auto&, const auto&) {});
	BOOST_TEST(restored.Execute("GetRules", request("GetRules")).count("tan:Rule") == 1u);
	BOOST_TEST(restored.SampleObject("VideoSource0", "7", 0.5, 0).empty());
	BOOST_TEST(restored.SampleObject("VideoSource0", "7", -0.5, 0).size() == 1u);
}
BOOST_AUTO_TEST_CASE(directions_object_identity_and_event_wire_format)
{
	call("CreateRules", rule("any") + rule("left", direction("Left")) + rule("right", direction("Right")));
	BOOST_TEST(sample(-0.5).empty());
	BOOST_TEST(sample(0.5, 0, "8").empty());
	auto right = sample(0.5);
	BOOST_REQUIRE_EQUAL(right.size(), 2u);
	BOOST_TEST(right.back().source_item_descriptions.back().second == "right");
	BOOST_TEST(sample(0.5).empty());
	auto left = sample(-0.5);
	BOOST_REQUIRE_EQUAL(left.size(), 2u);
	BOOST_TEST(left.back().source_item_descriptions.back().second == "left");
	BOOST_TEST(left.front().topic == utility::line::Topic);
	BOOST_TEST(left.front().property_operation.empty());
	BOOST_TEST(left.front().data_name == "ObjectId");
	BOOST_TEST(left.front().data_value == "7");
	BOOST_TEST(left.front().source_item_descriptions[1].first == "AnalyticsConfiguration");
	BOOST_TEST(left.front().source_item_descriptions[1].second == "AnalyticsConfig_0");
	auto serialized = osrv::event::serialize_notification_messages(left, "");
	const auto& message = serialized.get_child("wsnt:NotificationMessage.wsnt:Message.tt:Message");
	BOOST_TEST(!message.get_optional<std::string>("<xmlattr>.PropertyOperation"));
	BOOST_TEST(message.get<std::string>("tt:Data.tt:SimpleItem.<xmlattr>.Value") == "7");
	BOOST_TEST(detector.SampleObject("another-source", "7", 0.5, 0).empty());
	detector.ForgetObject("VideoSource0", "7");
	BOOST_TEST(sample(0.5).empty());
	BOOST_TEST(sample(std::numeric_limits<double>::quiet_NaN()).empty());
	BOOST_TEST(sample(-0.5, 0, "invalid id").empty());
}
BOOST_AUTO_TEST_CASE(horizontal_reversed_and_diagonal_segments)
{
	call("CreateRules",
			 rule("horizontal", direction("Left") + segments("<tt:Point x='-1' y='0'/><tt:Point x='1' y='0'/>")));
	BOOST_TEST(sample(0, -0.5).empty());
	BOOST_TEST(sample(0, 0.5).size() == 1u);
	BOOST_TEST(sample(0, -0.5).empty());
	call("ModifyRules", rule("horizontal", segments("<tt:Point x='1' y='0'/><tt:Point x='-1' y='0'/>")));
	BOOST_TEST(sample(0, 0.5).empty());
	BOOST_TEST(sample(0, -0.5).size() == 1u);
	call("ModifyRules", rule("horizontal", segments("<tt:Point x='-1' y='-1'/><tt:Point x='1' y='1'/>")));
	BOOST_TEST(sample(0, -0.5).empty());
	BOOST_TEST(sample(0, 0.5).size() == 1u);
}
BOOST_AUTO_TEST_CASE(touching_endpoints_vertices_and_finite_segments)
{
	call("CreateRules", rule("line", segments("<tt:Point x='0' y='-0.5'/><tt:Point x='0' y='0.5'/>")));
	BOOST_TEST(sample(-0.5).empty());
	BOOST_TEST(sample(0).empty());
	BOOST_TEST(sample(-0.5).empty()); // touches and returns to the same side
	BOOST_TEST(sample(0).empty());
	BOOST_TEST(sample(0.5).size() == 1u); // landing on the line defers until a crossing is established
	BOOST_TEST(sample(-0.5, 1).empty());	// passes beyond the upper endpoint
	BOOST_TEST(sample(0.5, 1).empty());
	call("ModifyRules",
			 rule("line", segments("<tt:Point x='-1' y='-0.5'/><tt:Point x='0' y='0'/><tt:Point x='1' y='-0.5'/>")));
	BOOST_TEST(sample(-0.5, 0).empty());
	BOOST_TEST(sample(0.5, 0).empty()); // tangent at the shared vertex
	call("ModifyRules",
			 rule("line", segments("<tt:Point x='-1' y='-0.5'/><tt:Point x='0' y='0'/><tt:Point x='1' y='0.5'/>")));
	BOOST_TEST(sample(-0.5, 0).empty());
	BOOST_TEST(sample(0.5, 0).size() == 1u); // crosses the shared vertex once
}
BOOST_AUTO_TEST_CASE(invalid_geometry_parameters_and_namespaces)
{
	for (const auto& pts :
			 {"", "<tt:Point x='0' y='0'/>", "<tt:Point x='0' y='0'/><tt:Point x='0' y='0'/>",
				"<tt:Point x='0' y='0'/><tt:Point x='2' y='1'/>", "<tt:Point x='NaN' y='0'/><tt:Point x='1' y='1'/>",
				"<tt:Point x='0' y='0'/><tt:Point x='1' y='0'/><tt:Point x='0.5' y='0'/>",
				"<tt:Point x='-1' y='-1'/><tt:Point x='1' y='1'/><tt:Point x='-1' y='1'/><tt:Point x='1' y='-1'/>"})
		BOOST_CHECK_THROW(call("CreateRules", rule("bad", segments(pts))), utility::RuleFault);
	BOOST_CHECK_THROW(call("CreateRules", rule("bad", direction("Entering"))), utility::RuleFault);
	BOOST_CHECK_THROW(call("CreateRules", rule("bad", direction("Any") + direction("Any"))), utility::RuleFault);
	BOOST_CHECK_THROW(call("CreateRules", rule("bad", "unexpected text")), utility::RuleFault);
	std::string tooMany;
	for (int i = 0; i < 9; ++i)
		tooMany += "<tt:Point x='" + std::to_string(i / 8.0) + "' y='0'/>";
	BOOST_CHECK_THROW(call("CreateRules", rule("bad", segments(tooMany))), utility::RuleFault);
	BOOST_CHECK_THROW(
			call("CreateRules", rule("bad", segments("<tt:Point x='0' y='0'/><tt:Point x='1' y='1'/>") + direction("Left"))),
			utility::RuleFault);
	BOOST_CHECK_THROW(call("CreateRules", rule("bad", "<tt:SimpleItem Name='PresetToken' Value='preset'/>")),
										utility::RuleFault);
	auto alias = rule("alias", direction("Left"), "q:LineDetector");
	alias.insert(alias.find("Name="), "xmlns:q='http://www.onvif.org/ver10/schema' ");
	call("CreateRules", alias);
	BOOST_CHECK_THROW(call("CreateRules", rule("bad", "", "tan:LineDetector")), utility::RuleFault);
	BOOST_TEST(saves == 1u);
}
BOOST_AUTO_TEST_CASE(mixed_batches_limits_and_track_reset)
{
	call("CreateRules", rule("line") + rule("motion", "", "tt:MotionRegionDetector"));
	sample(-0.5);
	BOOST_CHECK_THROW(call("ModifyRules", rule("line", direction("Left")) + rule("absent")), utility::RuleFault);
	BOOST_TEST(sample(0.5).size() == 1u); // failed batch preserves direction and track
	call("ModifyRules", rule("line", direction("Left")));
	BOOST_TEST(sample(-0.5).empty());								 // successful change starts a new track
	BOOST_TEST(detector.Synchronize().size() == 1u); // only Motion Region is a property
	try
	{
		call("ModifyRules", rule("motion"));
		BOOST_FAIL("Expected type change fault");
	}
	catch (const utility::RuleFault& f)
	{
		BOOST_TEST(f.detail == "TypeChangeProhibited");
		BOOST_TEST(f.receiver);
	}
	call("CreateRules", rule("two") + rule("three"));
	BOOST_CHECK_THROW(call("CreateRules", rule("overflow")), utility::RuleFault);
	BOOST_CHECK_THROW(call("DeleteRules", "<tan:RuleName>line</tan:RuleName><tan:RuleName>absent</tan:RuleName>"),
										utility::RuleFault);
	BOOST_TEST(call("GetRules").count("tan:Rule") == 4u);
	call("DeleteRules", "<tan:RuleName>line</tan:RuleName>");
	call("CreateRules", rule("line"));
	BOOST_TEST(sample(0.5).empty());
}
BOOST_AUTO_TEST_CASE(persistence_failure_preserves_object_history)
{
	call("CreateRules", rule("line"));
	Detector failing(profiles, [](const auto&, const auto&) { throw std::runtime_error("save failed"); });
	failing.SampleObject("VideoSource0", "1", -0.5, 0);
	BOOST_CHECK_THROW(failing.Execute("ModifyRules", request("ModifyRules", rule("line", direction("Left")))),
										utility::RuleFault);
	BOOST_TEST(failing.SampleObject("VideoSource0", "1", 0.5, 0).size() == 1u);
}
BOOST_AUTO_TEST_CASE(source_and_configuration_isolation)
{
	call("CreateRules", rule("line"));
	auto cfg = profiles.get_child("Analytics").front().second;
	cfg.put("token", "other-config");
	profiles.get_child("Analytics").push_back({"", cfg});
	auto src = profiles.get_child("VideoSource").front().second;
	src.put("token", "other-source-config");
	src.put("SourceToken", "other-source");
	Tree compatible, token;
	token.put_value("other-config");
	compatible.push_back({"", token});
	src.put_child("CompatibleAnalyticsConfigurations", compatible);
	profiles.get_child("VideoSource").push_back({"", src});
	Detector isolated(profiles, [](const auto&, const auto&) {});
	BOOST_TEST(isolated.VideoSources().size() == 2u);
	BOOST_TEST(isolated.SampleObject("VideoSource0", "1", -0.5, 0).empty());
	BOOST_TEST(isolated.SampleObject("other-source", "1", 0.5, 0).empty());
	auto crossed = isolated.SampleObject("VideoSource0", "1", 0.5, 0);
	BOOST_REQUIRE_EQUAL(crossed.size(), 1u);
	BOOST_TEST(crossed.front().source_item_descriptions[1].second == "AnalyticsConfig_0");
	BOOST_TEST(isolated.SampleObject("other-source", "1", -0.5, 0).size() == 1u);
	BOOST_CHECK_THROW(isolated.Execute("CreateRules", request("CreateRules", rule("a") + rule("b") + rule("c"))),
										utility::RuleFault);
}
BOOST_AUTO_TEST_SUITE_END()
