#include "../utility/AnalyticsRules.h"
#include <boost/property_tree/json_parser.hpp>
#include <boost/test/unit_test.hpp>

namespace
{
using Tree = boost::property_tree::ptree;
using Detector = utility::AnalyticsRules;
std::string request(const std::string& operation, const std::string& payload = "",
										const std::string& token = "AnalyticsConfig_0")
{
	return "<s:Envelope xmlns:s='http://www.w3.org/2003/05/soap-envelope' "
				 "xmlns:tan='http://www.onvif.org/ver20/analytics/wsdl' xmlns:tt='http://www.onvif.org/ver10/schema' "
				 "xmlns:axt='http://www.onvif.org/ver20/analytics'><s:Body><tan:" +
				 operation + ">" + "<tan:ConfigurationToken>" + token + "</tan:ConfigurationToken>" + payload +
				 "</tan:" + operation + "></s:Body></s:Envelope>";
}
std::string rule(const std::string& name, const std::string& value = "")
{
	return "<tan:Rule Name='" + name + "' Type='tt:MotionRegionDetector'><tt:Parameters>" + value +
				 "</tt:Parameters></tan:Rule>";
}
std::string region(const std::string& attrs, const std::string& body = "")
{
	return "<tt:ElementItem Name='MotionRegion'><axt:MotionRegionConfig " + attrs + ">" + body +
				 "</axt:MotionRegionConfig></tt:ElementItem>";
}
struct Fixture
{
	Tree profiles;
	unsigned saves = 0;
	static Tree LoadProfiles()
	{
		Tree result;
		boost::property_tree::read_json("../../server_configs/media_profiles.config", result);
		return result;
	}
	Fixture() : profiles(LoadProfiles())
	{
	}

	Detector detector{profiles, [this](const std::string&, const Tree& rules) {
											profiles.get_child("Analytics").front().second.put_child("rules", rules);
											++saves;
										}};
	Tree call(const std::string& op, const std::string& content = "")
	{
		return detector.Execute(op, request(op, content));
	}
};
} // namespace

BOOST_FIXTURE_TEST_SUITE(motion_regions, Fixture)
BOOST_AUTO_TEST_CASE(crud_defaults_persistence_and_atomic_batches)
{
	call("CreateRules", rule("front"));
	BOOST_TEST(saves == 1u);
	auto rules = call("GetRules");
	BOOST_TEST(rules.count("tan:Rule") == 1u);
	BOOST_TEST(rules.get<bool>("tan:Rule.tt:Parameters.tt:ElementItem.axt:MotionRegionConfig.<xmlattr>.Armed"));
	BOOST_CHECK_THROW(call("CreateRules", rule("second") + rule("front")), utility::RuleFault);
	BOOST_TEST(saves == 1u);
	BOOST_TEST(call("GetRules").count("tan:Rule") == 1u);
	BOOST_CHECK_THROW(call("ModifyRules", rule("front", region("Armed='false'")) + rule("missing")), utility::RuleFault);
	BOOST_TEST(detector.Synchronize().front().data_value == "false");
	call("ModifyRules", rule("front", region("Armed='false' Sensitivity='0.9'")));
	BOOST_TEST(
			!call("GetRules").get<bool>("tan:Rule.tt:Parameters.tt:ElementItem.axt:MotionRegionConfig.<xmlattr>.Armed"));
	Detector restored(profiles, [](const auto&, const auto&) {});
	BOOST_TEST(restored.Execute("GetRules", request("GetRules")).count("tan:Rule") == 1u);
	BOOST_CHECK_THROW(call("DeleteRules", "<tan:RuleName>front</tan:RuleName><tan:RuleName>missing</tan:RuleName>"),
										utility::RuleFault);
	BOOST_TEST(call("GetRules").count("tan:Rule") == 1u);
	call("DeleteRules", "<tan:RuleName>front</tan:RuleName>");
	BOOST_TEST(call("GetRules").empty());
}
BOOST_AUTO_TEST_CASE(geometry_sensitivity_and_state)
{
	std::string poly = "<tt:Polygon><tt:Point x='0' y='0'/><tt:Point x='200' y='0'/>"
										 "<tt:Point x='200' y='200'/><tt:Point x='0' y='200'/></tt:Polygon>";
	call("CreateRules", rule("front", region("Sensitivity='0.8' Armed='true'", poly)));
	auto initialized = detector.Sample(100, 100, 0);
	BOOST_TEST(initialized.front().property_operation == "Initialized");
	BOOST_TEST(detector.Sample(300, 100, 1).empty());
	BOOST_TEST(detector.Sample(100, 100, 0.1).empty());
	auto on = detector.Sample(100, 100, 0.5);
	BOOST_REQUIRE_EQUAL(on.size(), 1u);
	BOOST_TEST(on.front().topic == Detector::Topic);
	BOOST_TEST(on.front().data_value == "true");
	BOOST_TEST(on.front().source_item_descriptions.front().first == "VideoSource");
	BOOST_TEST(on.front().source_item_descriptions.front().second == "VideoSource0");
	BOOST_TEST(on.front().source_item_descriptions.back().second == "front");
	BOOST_TEST(detector.Synchronize().front().data_value == "true");
	BOOST_TEST(detector.Sample(100, 100, 0.5).empty());
	BOOST_TEST(detector.Sample(100, 100, 0).front().data_value == "false");
	call("ModifyRules", rule("front", region("Armed='false'")));
	detector.Sample(100, 100, 0);
	BOOST_TEST(detector.Sample(100, 100, 1).empty());
	BOOST_CHECK_THROW(call("ModifyRules", rule("front", region("Sensitivity='1.1'"))), utility::RuleFault);
	BOOST_CHECK_THROW(call("ModifyRules", rule("front", region("Armed='yes'"))), utility::RuleFault);
	BOOST_CHECK_THROW(call("ModifyRules", rule("front", region("Sensitivity='NaN'"))), utility::RuleFault);
	BOOST_CHECK_THROW(call("ModifyRules", rule("front", region("", "<axt:PresetToken>p0</axt:PresetToken>"))),
										utility::RuleFault);
	BOOST_CHECK_THROW(call("CreateRules", rule("bad", region("", "<tt:Polygon><tt:Point x='0' y='0'/></tt:Polygon>"))),
										utility::RuleFault);
	BOOST_CHECK_THROW(
			call("CreateRules", rule("bad", region("", "<tt:Polygon><tt:Point x='-1' y='0'/>"
																								 "<tt:Point x='5' y='0'/><tt:Point x='0' y='5'/></tt:Polygon>"))),
			utility::RuleFault);
	BOOST_CHECK_THROW(
			call("CreateRules",
					 rule("bad",
								region("", "<tt:Polygon><tt:Point x='0' y='0'/>"
													 "<tt:Point x='10' y='10'/><tt:Point x='0' y='10'/><tt:Point x='10' y='0'/></tt:Polygon>"))),
			utility::RuleFault);
}
BOOST_AUTO_TEST_CASE(namespaces_limits_faults_and_options)
{
	auto alias = rule("alias");
	auto pos = alias.find("Type=");
	alias.insert(pos, "xmlns:q='http://www.onvif.org/ver10/schema' ");
	pos = alias.find("Type='tt:");
	alias.replace(pos, 9, "Type='q:");
	call("CreateRules", alias);
	auto invalid = rule("bad");
	invalid.insert(invalid.find("Type="), "xmlns:tt='urn:other' ");
	BOOST_CHECK_THROW(call("CreateRules", invalid), utility::RuleFault);
	BOOST_CHECK_THROW(detector.Execute("GetRules", request("GetRules", "", "missing")), utility::RuleFault);
	BOOST_CHECK_THROW(call("GetRuleOptions", "<tan:RuleType>tt:Unknown</tan:RuleType>"), utility::RuleFault);
	auto filteredRequest = request("GetRuleOptions");
	auto tokenPosition = filteredRequest.find("<tan:ConfigurationToken>");
	filteredRequest.insert(
			tokenPosition,
			"<tan:RuleType xmlns:q='http://www.onvif.org/ver10/schema'> q:MotionRegionDetector </tan:RuleType>");
	BOOST_TEST(detector.Execute("GetRuleOptions", filteredRequest).count("tan:RuleOptions") == 1u);
	BOOST_CHECK_THROW(call("CreateRules", rule("bad") + "<tan:Unexpected/>"), utility::RuleFault);
	auto options = call("GetRuleOptions");
	BOOST_TEST(options.get<int>("tan:RuleOptions.axt:MotionRegionConfigOptions.axt:PolygonLimits.tt:Min") == 3);
	BOOST_TEST(options.get<std::string>("tan:RuleOptions.<xmlattr>.RuleType") == "tt:MotionRegionDetector");
	BOOST_TEST(!options.get<bool>("tan:RuleOptions.axt:MotionRegionConfigOptions.<xmlattr>.PTZPresetMotionSupport"));
	call("CreateRules", rule("two") + rule("three") + rule("four"));
	try
	{
		call("CreateRules", rule("five"));
		BOOST_FAIL("Expected rule limit fault");
	}
	catch (const utility::RuleFault& fault)
	{
		BOOST_TEST(fault.detail == "TooManyRules");
		BOOST_TEST(fault.receiver);
	}
	BOOST_TEST(call("GetRules").count("tan:Rule") == 4u);
}
BOOST_AUTO_TEST_CASE(persistence_failure_keeps_rules_and_events)
{
	Detector failing(profiles, [](const auto&, const auto&) { throw std::runtime_error("save failed"); });
	BOOST_CHECK_THROW(failing.Execute("CreateRules", request("CreateRules", rule("front"))), std::runtime_error);
	BOOST_TEST(failing.Execute("GetRules", request("GetRules")).empty());
	BOOST_TEST(failing.Sample(100, 100, 1).empty());
	try
	{
		failing.Execute("CreateRules", request("CreateRules", rule("front")));
		BOOST_FAIL("Expected save fault");
	}
	catch (const utility::RuleFault& fault)
	{
		BOOST_TEST(fault.detail == "ConfigurationConflict");
		BOOST_TEST(fault.receiver);
	}
}
BOOST_AUTO_TEST_SUITE_END()
