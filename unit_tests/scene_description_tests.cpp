#include "../onvif/Notification.h"
#include "../onvif/SceneDescription.h"
#include "../utility/AnalyticsRules.h"
#include "../utility/MetadataConfiguration.h"
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/test/unit_test.hpp>
#include <fstream>

namespace
{
using boost::property_tree::ptree;
using osrv::scene::Buffer;
} // namespace
BOOST_AUTO_TEST_SUITE(SceneDescription)
BOOST_AUTO_TEST_CASE(supported_sample_contains_license_plate_descriptor)
{
	auto xml = osrv::scene::FrameXml(osrv::scene::SampleFrame());
	bool found = false;
	for (const auto& [name, node] : xml)
		if (name == "tt:Object")
			if (auto plate = node.get_child_optional("tt:Appearance.tt:LicensePlateInfo"))
			{
				found = true;
				BOOST_TEST(plate->get<std::string>("tt:PlateNumber") == "TEST123");
				BOOST_TEST(plate->get<std::string>("tt:CountryCode") == "US");
				BOOST_TEST(plate->get<double>("tt:PlateNumber.<xmlattr>.Likelihood") == 0.95);
			}
	BOOST_TEST(found);
}
BOOST_AUTO_TEST_CASE(snapshots_are_immutable_scoped_and_independent)
{
	Buffer buffer;
	BOOST_TEST(buffer.Sample("source", {"analytics"}, "7", -0.25, 0, "2000-01-01T00:00:00Z", "Human") == "7");
	auto first = buffer.Read("source", "analytics", 0).frames.front();
	BOOST_TEST(buffer.Read("source", "analytics", 0).frames.front().get() == first.get());
	buffer.Sample("source", {"analytics"}, "7", 0.25, 0, "2000-01-01T00:00:02Z", "Human");
	auto next = buffer.Read("source", "analytics", first->sequence);
	BOOST_REQUIRE(next.frames.size() == 1);
	BOOST_TEST(first->objects.front().x == -0.25);
	BOOST_TEST(next.frames.front()->objects.front().x == 0.25);
	BOOST_TEST(buffer.Read("other", "analytics", 0).frames.empty());
	BOOST_TEST(buffer.Read("source", "other", 0).frames.empty());
}
BOOST_AUTO_TEST_CASE(human_descriptors_are_optional_and_follow_observations)
{
	Buffer buffer;
	auto face = osrv::scene::SampleHumanFace();
	face.yaw = -30;
	face.pitch = 15;
	face.roll = 5;
	face.opticals = true;
	buffer.Sample("source", {"analytics"}, "face", 0, 0, "2000-01-01T00:00:00Z", "HumanFace", {}, {},
								osrv::scene::FaceModule, {}, {}, face);
	auto first = buffer.Read("source", "analytics", 0).frames.front();
	auto appearance = osrv::scene::FrameXml(*first).get_child("tt:Object.tt:Appearance");
	BOOST_TEST(appearance.get<double>("tt:HumanFace.fc:PoseAngle.fc:PoseAngles.<xmlattr>.yaw") == -30);
	BOOST_TEST(appearance.get<double>("tt:HumanFace.fc:PoseAngle.fc:PoseAngles.<xmlattr>.pitch") == 15);
	BOOST_TEST(appearance.get<double>("tt:HumanFace.fc:PoseAngle.fc:PoseAngles.<xmlattr>.roll") == 5);
	BOOST_TEST(appearance.get<bool>("tt:HumanFace.fc:Accessory.fc:Opticals.fc:Wear"));
	BOOST_TEST(!appearance.get<bool>("tt:HumanFace.fc:Accessory.fc:Mask.fc:Wear"));
	BOOST_TEST(!appearance.get_child_optional("tt:HumanBody"));
	// A class label alone does not fabricate a descriptor; a new observation can omit it.
	buffer.Sample("source", {"analytics"}, "face", 0, 0, "2000-01-01T00:00:01Z", "HumanFace", {}, {},
								osrv::scene::FaceModule);
	auto latest = buffer.Read("source", "analytics", first->sequence).frames.front();
	BOOST_TEST(!osrv::scene::FrameXml(*latest).get_child_optional("tt:Object.tt:Appearance.tt:HumanFace"));
	BOOST_REQUIRE(first->objects.front().face);
	BOOST_TEST(first->objects.front().face->yaw == -30);
}
BOOST_AUTO_TEST_CASE(supported_scene_descriptors_match_the_synthetic_samples)
{
	ptree profiles;
	boost::property_tree::read_json("../../server_configs/media_profiles.config", profiles);
	utility::AnalyticsRules rules(profiles, [](const auto&, const auto&) {});
	auto tracker = osrv::scene::FrameXml(rules.SupportedScene(osrv::scene::ModuleName));
	const auto& body = tracker.get_child("tt:Object.tt:Appearance.tt:HumanBody");
	BOOST_TEST(body.get<std::string>("<xmlattr>.xmlns:bd") == "http://www.onvif.org/ver20/analytics/humanbody");
	BOOST_TEST(body.get<int>("bd:BodyMetric.bd:Height") == 175);
	BOOST_TEST(body.get<std::string>("bd:Clothing.bd:Tops.bd:Category") == "LongSleeve");
	BOOST_TEST(body.get<std::string>("bd:Clothing.bd:Bottoms.bd:Style") == "Jeans");
	BOOST_TEST(body.get<int>("bd:Clothing.bd:Tops.bd:Color.tt:ColorCluster.tt:Color.<xmlattr>.Y") == 100);
	BOOST_TEST(body.get<std::string>("bd:Behaviour.bd:Activity") == "Walking");
	auto face = osrv::scene::FrameXml(rules.SupportedScene(osrv::scene::FaceModule));
	BOOST_TEST(face.get<std::string>("tt:Object.tt:Appearance.tt:HumanFace.<xmlattr>.xmlns:fc") ==
						 "http://www.onvif.org/ver20/analytics/humanface");
	BOOST_TEST(face.get<double>("tt:Object.tt:Appearance.tt:HumanFace.fc:PoseAngle.fc:PoseAngles.<xmlattr>.yaw") == 0);
}
BOOST_AUTO_TEST_CASE(deletion_does_not_recycle_object_ids)
{
	Buffer buffer;
	auto id = buffer.Sample("source", {"analytics"}, "7", 0, 0, "2000-01-01T00:00:00Z");
	buffer.Forget("source", {"analytics"}, "7", "2000-01-01T00:00:01Z");
	auto deleted = buffer.Read("source", "analytics", 0).frames.front();
	BOOST_TEST(deleted->objects.empty());
	BOOST_REQUIRE(deleted->deleted.size() == 1);
	BOOST_TEST(deleted->deleted.front() == id);
	BOOST_TEST(buffer.Sample("source", {"analytics"}, "7", 0, 0, "2000-01-01T00:00:02Z") != id);
}
BOOST_AUTO_TEST_CASE(slow_readers_recover_current_state_without_blocking_fast_readers)
{
	Buffer buffer;
	buffer.Sample("source", {"analytics"}, "1", 0, 0, "2000-01-01T00:00:00Z");
	auto slow = buffer.Read("source", "analytics", 0).frames.front()->sequence;
	auto cursor = slow;
	for (unsigned i = 0; i < 200; ++i)
	{
		buffer.Sample("source", {"analytics"}, "1", double(i % 10) / 10, 0, "2000-01-01T00:00:01Z");
		auto batch = buffer.Read("source", "analytics", cursor);
		BOOST_REQUIRE(batch.frames.size() == 1);
		BOOST_TEST(!batch.skipped);
		cursor = batch.frames.front()->sequence;
	}
	auto recovered = buffer.Read("source", "analytics", slow);
	BOOST_TEST(recovered.skipped);
	BOOST_REQUIRE(recovered.frames.size() == 1);
	BOOST_TEST(recovered.frames.front()->sequence == cursor);
	// A quiet source's last snapshot survives a burst from other configurations.
	for (unsigned i = 0; i < 150; ++i)
		buffer.Sample("busy", {"other"}, "1", 0, 0, "2000-01-01T00:00:02Z");
	BOOST_REQUIRE(buffer.Read("source", "analytics", 0).frames.size() == 1);
	for (unsigned i = 2; i < 70; ++i)
		buffer.Sample("source", {"analytics"}, std::to_string(i), 0, 0, "2000-01-01T00:00:03Z");
	BOOST_TEST(buffer.Read("source", "analytics", 0).frames.front()->objects.size() == 64);
}
BOOST_AUTO_TEST_CASE(scene_frames_and_rule_events_use_the_same_sample_identity_and_time)
{
	ptree profiles;
	boost::property_tree::read_json("../../server_configs/media_profiles.config", profiles);
	utility::AnalyticsRules rules(profiles, [](const auto&, const auto&) {});
	rules.Execute(
			"CreateRules",
			"<s:Envelope xmlns:s='http://www.w3.org/2003/05/soap-envelope' "
			"xmlns:tan='http://www.onvif.org/ver20/analytics/wsdl' "
			"xmlns:tt='http://www.onvif.org/ver10/"
			"schema'><s:Body><tan:CreateRules><tan:ConfigurationToken>AnalyticsConfig_0</tan:ConfigurationToken><tan:Rule "
			"Name='scene-line' Type='tt:LineDetector'><tt:Parameters/></tan:Rule></tan:CreateRules></s:Body></s:Envelope>");
	rules.SampleObject("VideoSource0", "7", -0.25, 0, "Human");
	auto before = rules.Scenes()->Read("VideoSource0", "AnalyticsConfig_0", 0).frames.front();
	auto events = rules.SampleObject("VideoSource0", "7", 0.25, 0, "Human");
	BOOST_REQUIRE(events.size() == 1);
	auto frame = rules.Scenes()->Read("VideoSource0", "AnalyticsConfig_0", before->sequence).frames.front();
	BOOST_TEST(events.front().data_value == frame->objects.front().id);
	BOOST_TEST(events.front().utc_time == frame->utc_time);
	BOOST_TEST(frame->objects.front().classification == "Human");
	BOOST_REQUIRE(frame->objects.front().body);
	BOOST_TEST(osrv::scene::FrameXml(*frame).get<int>("tt:Object.tt:Appearance.tt:HumanBody.bd:BodyMetric.bd:Height") ==
						 175);
	auto xml = osrv::event::serialize_metadata(events, {}, {*frame});
	std::istringstream input(xml);
	ptree parsed;
	boost::property_tree::read_xml(input, parsed);
	BOOST_TEST(parsed.get<std::string>("tt:MetadataStream.tt:VideoAnalytics.tt:Frame.tt:Object.<xmlattr>.ObjectId") ==
						 "7");
	BOOST_TEST(parsed.get<std::string>(
								 "tt:MetadataStream.tt:VideoAnalytics.tt:Frame.tt:Object.tt:Appearance.tt:Class.tt:Type") == "Human");
	BOOST_TEST(parsed.get<std::string>(
								 "tt:MetadataStream.tt:Event.wsnt:NotificationMessage.wsnt:Message.tt:Message.<xmlattr>.UtcTime") ==
						 frame->utc_time);
	std::ofstream("stage5_scene.xml") << xml;
}
BOOST_AUTO_TEST_CASE(analytics_and_module_selection_are_independent_of_event_filters)
{
	ptree config;
	config.put("Analytics", true);
	config.put("Events", false);
	auto settings = osrv::metadata::Compile(config);
	BOOST_TEST(settings->analytics);
	BOOST_TEST(settings->tracker);
	BOOST_TEST(!settings->events);
	config.add_child("AnalyticsModules", ptree{});
	BOOST_TEST(!osrv::metadata::Compile(config)->tracker);
	ptree module;
	module.put_value("SyntheticObjectTracker");
	config.get_child("AnalyticsModules").push_back({"", module});
	BOOST_TEST(osrv::metadata::Compile(config)->tracker);
	config.get_child("AnalyticsModules").front().second.put_value("unsupported");
	BOOST_CHECK_THROW(osrv::metadata::Compile(config), osrv::metadata::Fault);
}
BOOST_AUTO_TEST_CASE(modules_have_independent_snapshots_and_global_object_ids)
{
	Buffer buffer;
	auto tracker = buffer.Sample("source", {"analytics"}, "7", 0, 0, "2000-01-01T00:00:00Z", "Human");
	auto plate = buffer.Sample("source", {"analytics"}, "plate", 0, 0, "2000-01-01T00:00:01Z", "LicensePlate", {}, {},
														 osrv::scene::LprModule);
	auto face = buffer.Sample("source", {"analytics"}, "face", 0, 0, "2000-01-01T00:00:02Z", "HumanFace", {}, {},
														osrv::scene::FaceModule);
	BOOST_TEST(tracker != plate);
	BOOST_TEST(face != plate);
	BOOST_TEST(tracker != face);
	auto batch = buffer.Read("source", "analytics", 0);
	BOOST_REQUIRE(batch.frames.size() == 3u);
	for (const auto& frame : batch.frames)
	{
		BOOST_TEST(frame->objects.size() == 1u);
		BOOST_TEST(osrv::scene::FrameXml(*frame).get<std::string>("<xmlattr>.Source") == frame->module);
	}
	ptree config;
	config.put("Analytics", true);
	config.add_child("AnalyticsModules", ptree{}).push_back({"", ptree(osrv::scene::LprModule)});
	auto selected = osrv::metadata::Compile(config);
	BOOST_TEST(!selected->tracker);
	BOOST_TEST(selected->modules.size() == 1u);
	BOOST_TEST(selected->modules.contains(osrv::scene::LprModule));
}

BOOST_AUTO_TEST_SUITE_END()
