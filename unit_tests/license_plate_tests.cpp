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
using Detector = utility::AnalyticsRules;
std::string request(const std::string& op, const std::string& body = "", bool options = false)
{
	return "<s:Envelope xmlns:s='http://www.w3.org/2003/05/soap-envelope' "
				 "xmlns:tan='http://www.onvif.org/ver20/analytics/wsdl' xmlns:tt='http://www.onvif.org/ver10/schema'>"
				 "<s:Body><tan:" +
				 op + ">" + (options ? "<tan:RuleType>tt:LicensePlateRecognition</tan:RuleType>" : "") +
				 "<tan:ConfigurationToken>AnalyticsConfig_0</tan:ConfigurationToken>" + body + "</tan:" + op +
				 "></s:Body></s:Envelope>";
}
std::string rule(const std::string& params = "", const std::string& name = "plates")
{
	return "<tan:Rule Name='" + name + "' Type='tt:LicensePlateRecognition'><tt:Parameters>" + params +
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
		Tree p;
		boost::property_tree::read_json("../../server_configs/media_profiles.config", p);
		p.get_child("Analytics").front().second.put_child("rules", Tree{});
		return p;
	}
	Tree profiles = Load();
	utility::counting::Time now{};
	bool fail = false;
	utility::lpr::Images images = utility::lpr::LoadImages("../../server_configs", "http://127.0.0.1:8080/");
	Detector detector{profiles,
										[this](const auto&, const Tree& rules) {
											if (fail)
												throw std::runtime_error("save failed");
											profiles.get_child("Analytics").front().second.put_child("rules", rules);
										},
										[this] { return now; }, images};
	utility::lpr::Sample plate{"TEST123", "US"};
	Tree call(const std::string& op, const std::string& body = "", bool options = false)
	{
		return detector.Execute(op, request(op, body, options));
	}
	auto sample()
	{
		return detector.SamplePlate("VideoSource0", plate);
	}
};
} // namespace

BOOST_FIXTURE_TEST_SUITE(license_plates, Fixture)
BOOST_AUTO_TEST_CASE(contract_roundtrip_restart_and_serialization)
{
	auto supported = call("GetSupportedRules").get_child("tan:SupportedRules");
	bool found = false;
	for (const auto& [key, desc] : supported)
		if (key == "tt:RuleDescription" && desc.get<std::string>("<xmlattr>.Name") == utility::lpr::Type)
		{
			found = true;
			BOOST_TEST(!desc.get<bool>("tt:Messages.<xmlattr>.IsProperty"));
			BOOST_TEST(desc.get<std::string>("tt:Messages.tt:ParentTopic") == utility::lpr::Topic);
			BOOST_TEST(desc.get<std::string>("tt:Messages.tt:Data.tt:ElementItemDescription.<xmlattr>.Type") ==
								 "tt:LicensePlateInfo");
		}
	BOOST_TEST(found);
	BOOST_TEST(call("GetRuleOptions", "", true).count("tan:RuleOptions") == 9u);
	call("CreateRules", rule());
	auto xml = call("GetRules");
	Detector restored(profiles, [](const auto&, const auto&) {}, [this] { return now; }, images);
	BOOST_CHECK(restored.Execute("GetRules", request("GetRules")) == xml);
	call("ModifyRules", rule());
	BOOST_CHECK(call("GetRules") == xml);
	auto events = sample();
	BOOST_REQUIRE_EQUAL(events.size(), 1u);
	BOOST_TEST(events.front().topic == utility::lpr::Topic);
	BOOST_TEST(events.front().property_operation.empty());
	BOOST_TEST(events.front().data_name == "Likelihood");
	BOOST_TEST(events.front().source_item_descriptions.size() == 2u);
	auto wire = osrv::event::serialize_notification_messages(events, "subscription");
	auto data = wire.get_child("wsnt:NotificationMessage.wsnt:Message.tt:Message.tt:Data");
	BOOST_TEST(data.count("tt:SimpleItem") == 5u);
	BOOST_TEST(data.count("tt:ElementItem") == 5u);
	BOOST_TEST(data.get<std::string>("tt:ElementItem.tt:LicensePlateInfo.tt:PlateNumber") == "TEST123");
	BOOST_TEST(data.get<std::string>("tt:ElementItem.tt:LicensePlateInfo.tt:CountryCode") == "US");
	for (const auto& [key, value] : data)
		if (key == "tt:ElementItem")
		{
			auto name = value.get<std::string>("<xmlattr>.Name");
			if (name == "Image" || name == "VehicleImage")
				BOOST_TEST(value.get<std::string>("tt:" + name) ==
									 (name == "Image" ? images.front().plate.base64 : images.front().vehicle.base64));
			if (name == "VehicleInfo")
				BOOST_TEST(value.get<std::string>("tt:VehicleInfo.tt:Type") == "Car");
			if (name == "BoundingBox")
				BOOST_TEST(value.get<double>("tt:BoundingBox.<xmlattr>.left") == -0.1);
		}
	std::vector<std::pair<std::string, std::string>> sources{{"VideoSource", "tt:ReferenceToken"}, {"Rule", "xs:string"}};
	std::vector<std::pair<std::string, std::string>> simpleData{{"Likelihood", "xs:float"}};
	osrv::event::EventPropertiesSerializer description(utility::lpr::Topic, sources, simpleData, false,
																										 {{"LicensePlateInfo", "tt:LicensePlateInfo"}});
	BOOST_TEST(description.Ptree().get<std::string>(
								 "LicensePlate.tt:MessageDescription.tt:Data.tt:ElementItemDescription.<xmlattr>.Name") ==
						 "LicensePlateInfo");
	BOOST_TEST(detector.Synchronize().empty());
	BOOST_TEST(detector.SynchronizeFields().empty());
	call("DeleteRules", "<tan:RuleName>plates</tan:RuleName>");
	BOOST_TEST(sample().empty());
}
BOOST_AUTO_TEST_CASE(repeat_interval_is_per_plate_and_transactional)
{
	call("CreateRules", rule(simple("EventInterval", "10")));
	BOOST_TEST(sample().size() == 1u);
	BOOST_TEST(sample().empty());
	plate.plate = "OTHER";
	BOOST_TEST(sample().size() == 1u);
	plate.plate = "TEST123";
	now += std::chrono::seconds(9);
	BOOST_TEST(sample().empty());
	fail = true;
	BOOST_CHECK_THROW(call("ModifyRules", rule()), utility::RuleFault);
	BOOST_TEST(sample().empty());
	fail = false;
	now += std::chrono::seconds(1);
	BOOST_TEST(sample().size() == 1u);
	BOOST_CHECK_THROW(call("ModifyRules", rule(simple("Threshold", "0")) + rule("", "missing")), utility::RuleFault);
	BOOST_TEST(sample().empty());
	call("ModifyRules", rule());
	BOOST_TEST(sample().size() == 1u);
	Detector restored(profiles, [](const auto&, const auto&) {}, [this] { return now; }, images);
	BOOST_TEST(restored.SamplePlate("VideoSource0", plate).size() == 1u);
}
BOOST_AUTO_TEST_CASE(filters_and_geometry)
{
	const std::string region = "<tt:ElementItem Name='Region'><tt:Polygon>"
														 "<tt:Point x='-0.5' y='-0.5'/><tt:Point x='0.5' y='-0.5'/>"
														 "<tt:Point x='0.5' y='0.5'/><tt:Point x='-0.5' y='0.5'/></tt:Polygon></tt:ElementItem>";
	call("CreateRules", rule(region + simple("Country", "US") + simple("PlateLocation", "Front") +
													 simple("Threshold", "0.9") + simple("PlateMinWidth", "5") + simple("PlateMaxWidth", "15")));
	BOOST_TEST(sample().size() == 1u);
	plate.x = 0.75;
	BOOST_TEST(sample().empty());
	plate.x = 0.5;
	BOOST_TEST(sample().size() == 1u);
	plate.country = "GB";
	BOOST_TEST(sample().empty());
	plate.country = "US";
	plate.location = "Rear";
	BOOST_TEST(sample().empty());
	plate.location = "Front";
	plate.width = 16;
	BOOST_TEST(sample().empty());
	plate.width = 10;
	plate.likelihood = 0.8;
	BOOST_TEST(sample().empty());
	plate.likelihood = 0.95;
	BOOST_TEST(detector.SamplePlate("other", plate).empty());
	const std::string snap = "<tt:ElementItem Name='SnapLine'><tt:Polyline>"
													 "<tt:Point x='0' y='-1'/><tt:Point x='0' y='1'/></tt:Polyline></tt:ElementItem>";
	call("ModifyRules", rule(snap));
	plate.x = -0.25;
	BOOST_TEST(sample().empty());
	plate.x = 0;
	BOOST_TEST(sample().empty());
	plate.x = 0.25;
	BOOST_TEST(sample().size() == 1u);
	plate.x = 0.4;
	BOOST_TEST(sample().empty());
	plate.x = -0.25;
	BOOST_TEST(sample().size() == 1u);
	plate.likelihood = std::numeric_limits<double>::quiet_NaN();
	BOOST_TEST(sample().empty());
}
BOOST_AUTO_TEST_CASE(invalid_parameters_and_shared_resource_limit)
{
	for (auto params :
			 {simple("IncludeImage", "RemoteStorage"), simple("PlateLocation", "Side"), simple("Sensitivity", "nan"),
				simple("Threshold", "1.1"), simple("EventInterval", "-1"), simple("EventInterval", "0.5"),
				simple("PlateMinWidth", "101"), simple("PlateMinWidth", "20") + simple("PlateMaxWidth", "10"),
				simple("Country", "US") + simple("Country", "GB"), simple("Unknown", "0"),
				std::string("<tt:ElementItem Name='Region'><tt:Polygon/></tt:ElementItem>"),
				std::string("<tt:ElementItem Name='SnapLine'><tt:Polygon/></tt:ElementItem>")})
	{
		BOOST_CHECK_EXCEPTION(call("CreateRules", rule(params)), utility::RuleFault,
													[](const auto& fault) { return fault.detail == "InvalidRule"; });
		BOOST_TEST(call("GetRules").empty());
	}
	for (int i = 0; i < 4; ++i)
		call("CreateRules", rule("", "rule" + std::to_string(i)));
	BOOST_CHECK_EXCEPTION(call("CreateRules", rule()), utility::RuleFault,
												[](const auto& fault) { return fault.detail == "TooManyRules" && fault.receiver; });
}
BOOST_AUTO_TEST_CASE(image_modes_and_optional_values)
{
	BOOST_TEST(images.front().plate.bytes.substr(0, 8) == std::string("\x89PNG\r\n\x1a\n", 8));
	BOOST_TEST(images.front().plate.bytes != images.front().vehicle.bytes);
	BOOST_TEST(images.front().plate.base64 != images.front().vehicle.base64);
	BOOST_TEST(images.front().vehicle.bytes.substr(0, 8) == std::string("\x89PNG\r\n\x1a\n", 8));
	BOOST_TEST(images.front().plateUri == "http://127.0.0.1:8080/analytics/lpr/plate.png");
	call("CreateRules", rule(simple("IncludeImage", "LocalStorage")));
	auto events = sample();
	BOOST_REQUIRE_EQUAL(events.size(), 1u);
	BOOST_TEST(events.front().data_element_items.size() == 3u);
	BOOST_TEST(events.front().data_simple_items.size() == 4u);
	call("ModifyRules", rule(simple("IncludeImage", "")));
	events = sample();
	BOOST_REQUIRE_EQUAL(events.size(), 1u);
	BOOST_TEST(events.front().data_element_items.size() == 3u);
	BOOST_TEST(events.front().data_simple_items.size() == 2u);
	plate.direction = "Left";
	plate.speed = 8;
	plate.x = -1;
	events = sample();
	BOOST_TEST(events.front().data_simple_items[0].second == "8.000000");
	BOOST_TEST(events.front().data_simple_items[1].second == "Left");
	BOOST_TEST(events.front().data_element_items[1].get<double>("tt:BoundingBox.<xmlattr>.left") == -1);
	plate.speed = -1;
	BOOST_TEST(sample().empty());
	plate.speed = 8;
	plate.direction = "Unknown";
	BOOST_TEST(sample().empty());
	BOOST_CHECK_THROW(utility::lpr::LoadImages("/nonexistent-lpr-fixture", "http://localhost/"), std::runtime_error);
}
BOOST_AUTO_TEST_CASE(vehicle_catalog_cycle_images_and_independent_repeat_history)
{
	const auto& vehicles = utility::lpr::TestVehicles();
	BOOST_REQUIRE_EQUAL(images.size(), 5u);
	BOOST_REQUIRE_EQUAL(vehicles.size(), 5u);
	BOOST_TEST(utility::lpr::ImagesReady(images));
	call("CreateRules", rule(simple("EventInterval", "10")));
	std::set<std::string> plates, uris;
	for (size_t index = 0; index < vehicles.size(); ++index)
	{
		auto input = utility::lpr::SyntheticSample(index);
		BOOST_TEST(input.plate == vehicles[index].plate);
		BOOST_TEST(input.x == -0.75);
		auto events = detector.SamplePlate("VideoSource0", input);
		BOOST_REQUIRE_EQUAL(events.size(), 1u);
		const auto& event = events.front();
		plates.insert(input.plate);
		for (const auto& item : event.data_element_items)
		{
			auto name = item.get<std::string>("<xmlattr>.Name");
			if (name == "LicensePlateInfo")
			{
				BOOST_TEST(item.get<std::string>("tt:LicensePlateInfo.tt:PlateNumber") == input.plate);
				BOOST_TEST(item.get<std::string>("tt:LicensePlateInfo.tt:CountryCode") == input.country);
			}
			if (name == "VehicleInfo")
			{
				BOOST_TEST(item.get<std::string>("tt:VehicleInfo.tt:Type") == input.vehicleType);
				BOOST_TEST(item.get<std::string>("tt:VehicleInfo.tt:Brand") == input.vehicleBrand);
				BOOST_TEST(item.get<std::string>("tt:VehicleInfo.tt:Model") == input.vehicleModel);
			}
			if (name == "Image")
				BOOST_CHECK(item.get<std::string>("tt:Image") == images[index].plate.base64);
			if (name == "VehicleImage")
				BOOST_CHECK(item.get<std::string>("tt:VehicleImage") == images[index].vehicle.base64);
		}
		BOOST_TEST(event.data_simple_items[2].second == images[index].plateUri);
		BOOST_TEST(event.data_simple_items[3].second == images[index].vehicleUri);
		uris.insert(images[index].plateUri);
		uris.insert(images[index].vehicleUri);
		for (size_t other = 0; other < index; ++other)
		{
			BOOST_CHECK(images[index].plate.bytes != images[other].plate.bytes);
			BOOST_CHECK(images[index].vehicle.bytes != images[other].vehicle.bytes);
		}
		now += std::chrono::seconds(1);
	}
	BOOST_TEST(plates.size() == vehicles.size());
	BOOST_TEST(uris.size() == vehicles.size() * 2);
	BOOST_TEST(detector.SamplePlate("VideoSource0", utility::lpr::SyntheticSample(vehicles.size())).empty());
	now += std::chrono::seconds(10 - vehicles.size());
	BOOST_TEST(detector.SamplePlate("VideoSource0", utility::lpr::SyntheticSample(vehicles.size())).size() == 1u);
	for (size_t index = 0; index < vehicles.size(); ++index)
	{
		BOOST_TEST(utility::lpr::SyntheticSample(2 * vehicles.size() + index).direction == "Right");
		BOOST_TEST(utility::lpr::SyntheticSample(2 * vehicles.size() + index).speed == vehicles[index].speed);
		BOOST_TEST(utility::lpr::SyntheticSample(6 * vehicles.size() + index).direction == "Left");
		BOOST_TEST(utility::lpr::SyntheticSample(4 * vehicles.size() + index).speed == 0);
		BOOST_TEST(utility::lpr::SyntheticSample(8 * vehicles.size() + index).plate == vehicles[index].plate);
	}
	BOOST_TEST(vehicles.back().vehicleType == "Motorcycle");
	BOOST_TEST(vehicles.back().plate == "TEST654");
	BOOST_TEST(vehicles.back().location == "Rear");
	for (const auto& location : {"Rear", "Front"})
	{
		call("ModifyRules", rule(simple("PlateLocation", location)));
		size_t emitted = 0;
		for (const auto& vehicle : vehicles)
			emitted += detector.SamplePlate("VideoSource0", vehicle).size();
		BOOST_TEST(emitted == (std::string(location) == "Rear" ? 1u : vehicles.size() - 1));
	}
	plate.imageIndex = vehicles.size();
	BOOST_TEST(sample().empty());
}
BOOST_AUTO_TEST_SUITE_END()
