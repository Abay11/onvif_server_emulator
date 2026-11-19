#include "../utility/AnalyticsRules.h"
#include "../utility/EventService.h"
#include "../utility/FieldDetector.h"
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
std::string rule(const std::string& name = "zone", const std::string& params = "")
{
	return "<tan:Rule Name='" + name + "' Type='tt:FieldDetector'><tt:Parameters>" + params +
				 "</tt:Parameters></tan:Rule>";
}
std::string field(const std::string& points)
{
	return "<tt:ElementItem Name='Field'><tt:Polygon>" + points + "</tt:Polygon></tt:ElementItem>";
}
const std::string Square = "<tt:Point x='-0.5' y='-0.5'/><tt:Point x='0.5' y='-0.5'/>"
													 "<tt:Point x='0.5' y='0.5'/><tt:Point x='-0.5' y='0.5'/>";
struct Fixture
{
	static Tree Load()
	{
		Tree p;
		boost::property_tree::read_json("../../server_configs/media_profiles.config", p);
		return p;
	}
	Tree profiles = Load();
	bool failSave = false;
	unsigned saves = 0;
	Detector detector{profiles, [this](const std::string&, const Tree& rules) {
											if (failSave)
												throw std::runtime_error("save failed");
											profiles.get_child("Analytics").front().second.put_child("rules", rules);
											++saves;
										}};
	Tree call(const std::string& op, const std::string& body = "", const std::string& type = "")
	{
		return detector.Execute(op, request(op, body, type));
	}
	auto sample(double x, double y = 0, const std::string& id = "1")
	{
		return detector.SampleObject("VideoSource0", id, x, y);
	}
	void create()
	{
		call("CreateRules", rule("zone", field(Square)));
		auto events = detector.TickFields();
		BOOST_REQUIRE_EQUAL(events.size(), 1u);
		BOOST_TEST(events.front().property_operation == "Initialized");
		BOOST_TEST(events.front().data_value == "false");
	}
};
} // namespace

BOOST_FIXTURE_TEST_SUITE(field_detectors, Fixture)
BOOST_AUTO_TEST_CASE(discovery_options_defaults_and_persistence)
{
	auto desc = call("GetSupportedRules").get_child("tan:SupportedRules").back().second;
	BOOST_TEST(desc.get<std::string>("<xmlattr>.Name") == utility::field::Type);
	BOOST_TEST(desc.get<bool>("tt:Messages.<xmlattr>.IsProperty"));
	BOOST_TEST(desc.get<std::string>("tt:Parameters.tt:ElementItemDescription.<xmlattr>.Type") == "tt:Polygon");
	BOOST_TEST(desc.get<std::string>("tt:Messages.tt:ParentTopic") == utility::field::Topic);
	auto opts = call("GetRuleOptions", "", utility::field::Type);
	BOOST_REQUIRE_EQUAL(opts.size(), 1u);
	BOOST_TEST(opts.front().second.get<int>("tt:PolygonOptions.tt:VertexLimits.tt:Min") == 3);
	BOOST_TEST(opts.front().second.get<int>("tt:PolygonOptions.tt:VertexLimits.tt:Max") == 8);
	call("CreateRules", rule());
	auto xml = call("GetRules").get_child("tan:Rule");
	BOOST_TEST(xml.get_child("tt:Parameters.tt:ElementItem.tt:Polygon").size() == 4u);
	detector.TickFields();
	BOOST_TEST(sample(-1, -1).front().data_value == "true");
	Detector restored(profiles, [](const auto&, const auto&) {});
	BOOST_TEST(restored.SynchronizeFields().front().data_value == "false");
	BOOST_CHECK(restored.Execute("GetRules", request("GetRules")).get_child("tan:Rule") == xml);
	call("ModifyRules", rule());
	BOOST_CHECK(call("GetRules").get_child("tan:Rule") == xml);
}
BOOST_AUTO_TEST_CASE(multiple_objects_boundary_and_disappearance)
{
	create();
	BOOST_TEST(sample(0.75).empty());
	auto events = sample(0);
	BOOST_REQUIRE_EQUAL(events.size(), 1u);
	BOOST_TEST(events.front().data_value == "true");
	BOOST_TEST(sample(0, 0, "2").empty());
	BOOST_TEST(sample(0.75).empty());
	BOOST_TEST(detector.SynchronizeFields().front().data_value == "true");
	detector.ForgetObject("other-source", "2");
	BOOST_TEST(detector.TickFields().empty());
	detector.ForgetObject("VideoSource0", "2");
	events = detector.TickFields();
	BOOST_REQUIRE_EQUAL(events.size(), 1u);
	BOOST_TEST(events.front().data_value == "false");
	BOOST_TEST(sample(0.5, 0.5).front().data_value == "true"); // boundary included
	BOOST_TEST(sample(0.5, 0).empty());
	BOOST_TEST(sample(0.75).front().data_value == "false");
	BOOST_TEST(sample(std::numeric_limits<double>::quiet_NaN()).empty());
	BOOST_TEST(sample(0, 0, "bad id").empty());
	BOOST_TEST(detector.SampleObject("other-source", "1", 0, 0).empty());
}
BOOST_AUTO_TEST_CASE(concave_polygon_and_orientation)
{
	const std::string concave = "<tt:Point x='-1' y='-1'/><tt:Point x='1' y='-1'/>"
															"<tt:Point x='0' y='0'/><tt:Point x='1' y='1'/><tt:Point x='-1' y='1'/>";
	call("CreateRules", rule("zone", field(concave)));
	detector.TickFields();
	BOOST_TEST(sample(0.75).empty());
	BOOST_TEST(sample(-0.5).front().data_value == "true");
	call("ModifyRules", rule("zone", field("<tt:Point x='-0.5' y='0.5'/><tt:Point x='0.5' y='0.5'/>"
																				 "<tt:Point x='0.5' y='-0.5'/><tt:Point x='-0.5' y='-0.5'/>")));
	detector.TickFields();
	BOOST_TEST(sample(0).front().data_value == "true");
}
BOOST_AUTO_TEST_CASE(invalid_geometry_structure_and_namespace)
{
	for (const auto& points :
			 {"", "<tt:Point x='0' y='0'/>", "<tt:Point x='0' y='0'/><tt:Point x='1' y='1'/><tt:Point x='-1' y='-1'/>",
				"<tt:Point x='-1' y='-1'/><tt:Point x='1' y='1'/><tt:Point x='-1' y='1'/><tt:Point x='1' y='-1'/>",
				"<tt:Point x='0' y='0'/><tt:Point x='2' y='0'/><tt:Point x='0' y='1'/>",
				"<tt:Point x='NaN' y='0'/><tt:Point x='1' y='0'/><tt:Point x='0' y='1'/>",
				"<tt:Point x='INF' y='0'/><tt:Point x='1' y='0'/><tt:Point x='0' y='1'/>"})
		BOOST_CHECK_THROW(call("CreateRules", rule("bad", field(points))), utility::RuleFault);
	BOOST_CHECK_THROW(call("CreateRules", rule("bad", field(Square + "<tt:Point x='-0.5' y='-0.5'/>"))),
										utility::RuleFault);
	std::string many;
	for (int i = 0; i < 9; ++i)
		many += "<tt:Point x='" + std::to_string(i / 8.0) + "' y='0'/>";
	BOOST_CHECK_THROW(call("CreateRules", rule("bad", field(many))), utility::RuleFault);
	for (const auto& param : {field(Square) + field(Square), field(Square) + "text",
														std::string("<tt:SimpleItem Name='ClassFilter' Value='Human'/>"),
														std::string("<tt:ElementItem Name='Field'><tt:Polygon bad='true'/></tt:ElementItem>"),
														field("<tt:Point x='0' y='0' z='1'/><tt:Point x='1' y='0'/><tt:Point x='0' y='1'/>")})
		BOOST_CHECK_THROW(call("CreateRules", rule("bad", param)), utility::RuleFault);
	BOOST_TEST(saves == 0u);
	auto alias = rule();
	alias.replace(alias.find("tt:FieldDetector"), 16, "q:FieldDetector");
	alias.insert(alias.find("Name="), "xmlns:q='http://www.onvif.org/ver10/schema' ");
	call("CreateRules", alias);
}
BOOST_AUTO_TEST_CASE(transaction_rollback_lifecycle_and_event_wire)
{
	create();
	sample(0);
	BOOST_CHECK_THROW(call("ModifyRules", rule() + rule("missing")), utility::RuleFault);
	BOOST_CHECK_THROW(call("DeleteRules", "<tan:RuleName>zone</tan:RuleName><tan:RuleName>missing</tan:RuleName>"),
										utility::RuleFault);
	failSave = true;
	BOOST_CHECK_THROW(call("ModifyRules", rule()), utility::RuleFault);
	BOOST_CHECK_THROW(call("DeleteRules", "<tan:RuleName>zone</tan:RuleName>"), utility::RuleFault);
	BOOST_TEST(detector.TickFields().empty());
	BOOST_TEST(sample(0).empty());
	BOOST_TEST(detector.SynchronizeFields().front().data_value == "true");
	failSave = false;
	call("ModifyRules", rule());
	auto changed = detector.TickFields();
	BOOST_REQUIRE_EQUAL(changed.size(), 1u);
	BOOST_TEST(changed.front().property_operation == "Changed");
	BOOST_TEST(changed.front().data_value == "false");
	call("DeleteRules", "<tan:RuleName>zone</tan:RuleName>");
	auto deleted = detector.TickFields();
	BOOST_REQUIRE_EQUAL(deleted.size(), 1u);
	BOOST_TEST(deleted.front().property_operation == "Deleted");
	BOOST_TEST(detector.SynchronizeFields().empty());
	const auto& msg = deleted.front();
	BOOST_TEST(msg.source_item_descriptions[0].first == "VideoSource");
	BOOST_TEST(msg.source_item_descriptions[1].second == "AnalyticsConfig_0");
	BOOST_TEST(msg.source_item_descriptions[2].second == "zone");
	auto wire = osrv::event::serialize_notification_messages(deleted, "");
	BOOST_TEST(
			wire.get<std::string>("wsnt:NotificationMessage.wsnt:Message.tt:Message.tt:Data.tt:SimpleItem.<xmlattr>.Name") ==
			"IsInside");
}
BOOST_AUTO_TEST_CASE(shared_limit_and_type_change)
{
	call("CreateRules", rule("1") + rule("2") + rule("3") + rule("4"));
	BOOST_CHECK_THROW(call("CreateRules", rule("5")), utility::RuleFault);
	auto other = rule("1");
	other.replace(other.find("tt:FieldDetector"), 16, "tt:LineDetector");
	BOOST_CHECK_THROW(call("ModifyRules", other), utility::RuleFault);
	BOOST_TEST(call("GetRules").count("tan:Rule") == 4u);
}
BOOST_AUTO_TEST_SUITE_END()
