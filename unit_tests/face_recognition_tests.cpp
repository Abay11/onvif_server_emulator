#include "../onvif_services/pullpoint/pull_point.h"
#include "../utility/AnalyticsRules.h"
#include "../utility/EventService.h"
#include <boost/property_tree/json_parser.hpp>
#include <boost/test/unit_test.hpp>
#include <limits>
#include <set>

namespace
{
using Tree = boost::property_tree::ptree;
std::string request(const std::string& op, const std::string& body = "", bool options = false)
{
	return "<s:Envelope xmlns:s='http://www.w3.org/2003/05/soap-envelope' "
				 "xmlns:tan='http://www.onvif.org/ver20/analytics/wsdl' xmlns:tt='http://www.onvif.org/ver10/schema'>"
				 "<s:Body><tan:" +
				 op + ">" + (options ? "<tan:RuleType>tt:FaceRecognition</tan:RuleType>" : "") +
				 "<tan:ConfigurationToken>AnalyticsConfig_0</tan:ConfigurationToken>" + body + "</tan:" + op +
				 "></s:Body></s:Envelope>";
}
std::string rule(const std::string& params = "", const std::string& name = "faces")
{
	return "<tan:Rule Name='" + name + "' Type='tt:FaceRecognition'><tt:Parameters>" + params +
				 "</tt:Parameters></tan:Rule>";
}
std::string simple(const std::string& name, const std::string& value)
{
	return "<tt:SimpleItem Name='" + name + "' Value='" + value + "'/>";
}
struct Fixture
{
	static Tree Load()
	{
		Tree result;
		boost::property_tree::read_json("../../server_configs/media_profiles.config", result);
		result.get_child("Analytics").front().second.put_child("rules", Tree{});
		return result;
	}
	Tree profiles = Load();
	bool fail = false;
	utility::face::Images images = utility::face::LoadImages("../../server_configs", "http://192.0.2.1:9090/");
	utility::AnalyticsRules detector{profiles,
																	 [this](const auto&, const Tree& rules) {
																		 if (fail)
																			 throw std::runtime_error("save failed");
																		 profiles.get_child("Analytics").front().second.put_child("rules", rules);
																	 },
																	 std::chrono::system_clock::now,
																	 {},
																	 images};
	Tree call(const std::string& op, const std::string& body = "", bool options = false)
	{
		return detector.Execute(op, request(op, body, options));
	}
	auto sample(size_t index = 0, double likelihood = 0.98)
	{
		return detector.SampleFace("VideoSource0", {index, likelihood});
	}
};
} // namespace
BOOST_FIXTURE_TEST_SUITE(face_recognition, Fixture)
BOOST_AUTO_TEST_CASE(scene_identity_timestamp_and_module_match_recognition)
{
	call("CreateRules", rule());
	auto events = sample(0);
	BOOST_REQUIRE(events.size() == 1u);
	auto batch = detector.Scenes()->Read("VideoSource0", "AnalyticsConfig_0", 0);
	BOOST_REQUIRE(batch.frames.size() == 1u);
	const auto& frame = *batch.frames.front();
	BOOST_TEST(frame.module == osrv::scene::FaceModule);
	BOOST_TEST(frame.utc_time == events.front().utc_time);
	BOOST_TEST(frame.objects.front().classification == "HumanFace");
	BOOST_TEST(frame.objects.front().id ==
						 events.front().data_element_items[1].get<std::string>("tt:ObjectId.<xmlattr>.ObjectId"));
	auto xml = osrv::scene::FrameXml(frame);
	BOOST_TEST(xml.get<std::string>("tt:Object.tt:Appearance.tt:ImageRef") == images.front().previewUri);
	BOOST_TEST(xml.get<double>("tt:Object.tt:Appearance.tt:Shape.tt:BoundingBox.<xmlattr>.left") == -0.45);
}
BOOST_AUTO_TEST_CASE(contract_images_and_serialization)
{
	bool found = false;
	auto supported = call("GetSupportedRules").get_child("tan:SupportedRules");
	for (const auto& [key, desc] : supported)
		if (key == "tt:RuleDescription" && desc.get<std::string>("<xmlattr>.Name") == utility::face::Type)
		{
			found = true;
			BOOST_TEST(!desc.get<bool>("tt:Messages.<xmlattr>.IsProperty"));
			BOOST_TEST(desc.get<std::string>("tt:Messages.tt:ParentTopic") == utility::face::Topic);
			BOOST_TEST(desc.get_child("tt:Messages.tt:Data").count("tt:ElementItemDescription") == 3u);
		}
	BOOST_TEST(found);
	BOOST_TEST(call("GetRuleOptions", "", true).count("tan:RuleOptions") == 2u);
	call("CreateRules", rule());
	std::set<std::string> ids, uris, binaries;
	for (size_t i = 0; i < 3; ++i)
	{
		auto events = sample(i);
		BOOST_REQUIRE_EQUAL(events.size(), 1u);
		const auto& msg = events.front();
		BOOST_TEST(msg.topic == utility::face::Topic);
		BOOST_TEST(msg.property_operation.empty());
		BOOST_TEST(msg.source_item_descriptions[0].second == "VideoSource0");
		BOOST_TEST(msg.source_item_descriptions[1].second == "faces");
		BOOST_TEST(msg.data_name == "Likelihood");
		BOOST_TEST(std::stod(msg.data_value) == 0.98);
		BOOST_TEST(msg.data_simple_items.size() == 4u);
		BOOST_TEST(msg.data_element_items.size() == 3u);
		ids.insert(msg.data_simple_items[0].second);
		uris.insert(msg.data_simple_items[2].second);
		BOOST_TEST(msg.data_simple_items[2].second == images[i].uri);
		BOOST_TEST(msg.data_simple_items[3].first == "ImageUri");
		BOOST_TEST(msg.data_simple_items[3].second == images[i].uri);
		BOOST_TEST(images[i].uri == "http://192.0.2.1:9090" + utility::face::ImagePath(i));
		BOOST_TEST(images[i].image.bytes.substr(0, 8) == std::string("\x89PNG\r\n\x1a\n", 8));
		binaries.insert(images[i].image.bytes);
		BOOST_TEST(msg.data_element_items[0].get<double>("tt:BoundingBox.<xmlattr>.left") == -0.45);
		BOOST_TEST(msg.data_element_items[1].get<int>("tt:ObjectId.<xmlattr>.ObjectId") == i + 1);
		BOOST_CHECK(msg.data_element_items[2].get<std::string>("tt:Image") == images[i].image.base64);
		auto serialized = osrv::event::serialize_notification_messages(events, "subscription");
		const auto& data = serialized.get_child("wsnt:NotificationMessage.wsnt:Message.tt:Message.tt:Data");
		BOOST_TEST(data.count("tt:SimpleItem") == 5u);
		BOOST_TEST(data.count("tt:ElementItem") == 3u);
	}
	BOOST_TEST(ids.size() == 3u);
	BOOST_TEST(uris.size() == 3u);
	BOOST_TEST(binaries.size() == 3u);
	BOOST_TEST(detector.Synchronize().empty());
	BOOST_TEST(detector.SynchronizeCounts().empty());
	BOOST_TEST(detector.SynchronizeFields().empty());
}
BOOST_AUTO_TEST_CASE(modes_threshold_persistence_and_delete)
{
	call("CreateRules", rule(simple("IncludeImage", "LocalStorage") + simple("Threshold", "0.98")));
	BOOST_TEST(sample(0, 0.97).empty());
	auto events = sample();
	BOOST_REQUIRE_EQUAL(events.size(), 1u);
	BOOST_TEST(events.front().data_element_items.size() == 2u);
	BOOST_TEST(events.front().data_simple_items.size() == 4u);
	call("ModifyRules", rule(simple("IncludeImage", "")));
	events = sample();
	BOOST_REQUIRE_EQUAL(events.size(), 1u);
	BOOST_TEST(events.front().data_simple_items.size() == 3u);
	BOOST_TEST(events.front().data_element_items.size() == 2u);
	BOOST_TEST(call("GetRules").get<std::string>("tan:Rule.<xmlattr>.Type") == utility::face::Type);
	utility::AnalyticsRules restored(
			profiles, [](const auto&, const auto&) {}, std::chrono::system_clock::now, {}, images);
	BOOST_TEST(restored.SampleFace("VideoSource0", {0, 0.97}).empty());
	BOOST_TEST(restored.SampleFace("VideoSource0", {}).size() == 1u);
	fail = true;
	BOOST_CHECK_THROW(call("ModifyRules", rule(simple("Threshold", "1"))), utility::RuleFault);
	BOOST_TEST(sample().size() == 1u);
	fail = false;
	BOOST_CHECK_THROW(call("ModifyRules", rule(simple("Threshold", "1")) + rule("", "missing")), utility::RuleFault);
	BOOST_TEST(sample().size() == 1u);
	call("DeleteRules", "<tan:RuleName>faces</tan:RuleName>");
	BOOST_TEST(sample().empty());
}
BOOST_AUTO_TEST_CASE(faults_resource_limit_and_invalid_samples)
{
	for (const auto& params :
			 {simple("IncludeImage", "RemoteStorage"), simple("IncludeImage", "invalid"), simple("Threshold", "nan"),
				simple("Threshold", "-0.1"), simple("Threshold", "1.1"), simple("Unknown", "0"),
				simple("IncludeImage", "") + simple("IncludeImage", "Embedded"), std::string("<tt:ElementItem Name='Image'/>")})
		BOOST_CHECK_EXCEPTION(call("CreateRules", rule(params)), utility::RuleFault,
													[](const auto& fault) { return fault.detail == "InvalidRule"; });
	BOOST_TEST(call("GetRules").empty());
	for (int i = 0; i < 4; ++i)
		call("CreateRules", rule("", "faces" + std::to_string(i)));
	BOOST_CHECK_EXCEPTION(call("CreateRules", rule()), utility::RuleFault,
												[](const auto& fault) { return fault.detail == "TooManyRules" && fault.receiver; });
	BOOST_TEST(sample(3).empty());
	BOOST_TEST(sample(0, std::numeric_limits<double>::quiet_NaN()).empty());
	BOOST_TEST(sample(0, -0.1).empty());
	BOOST_TEST(sample(0, 1.1).empty());
	BOOST_TEST(detector.SampleFace("unknown", {}).empty());
	BOOST_CHECK_THROW(utility::face::LoadImages("/nonexistent-face-fixtures", "http://localhost"), std::runtime_error);
	utility::AnalyticsRules noImages(Load(), [](const auto&, const auto&) {});
	BOOST_CHECK_EXCEPTION(noImages.Execute("CreateRules", request("CreateRules", rule())), utility::RuleFault,
												[](const auto& fault) { return fault.detail == "ConfigurationConflict" && fault.receiver; });
}
BOOST_AUTO_TEST_SUITE_END()
