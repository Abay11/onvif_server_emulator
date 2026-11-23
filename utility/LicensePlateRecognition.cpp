#include "LicensePlateRecognition.h"
#include "AnalyticsRules.h"
#include "FieldDetector.h"
#include "LineDetector.h"
#include <algorithm>
#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/transform_width.hpp>
#include <cmath>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <vector>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>

namespace utility::lpr
{
namespace
{
Image encodedImage(std::string bytes)
{
	Image result;
	result.bytes = std::move(bytes);
	using namespace boost::archive::iterators;
	using Encoder = base64_from_binary<transform_width<std::string::const_iterator, 6, 8>>;
	result.base64.assign(Encoder(result.bytes.begin()), Encoder(result.bytes.end()));
	result.base64.append((3 - result.bytes.size() % 3) % 3, '=');
	return result;
}
Image thumbnail(const Image& original)
{
	GError* error = nullptr;
	if (!gst_init_check(nullptr, nullptr, &error))
	{
		std::string reason = error ? error->message : "unknown error";
		g_clear_error(&error);
		throw std::runtime_error("Cannot initialize scene image conversion: " + reason);
	}
	auto cleanup = [](GstElement* element) {
		gst_element_set_state(element, GST_STATE_NULL);
		gst_object_unref(element);
	};
	std::unique_ptr<GstElement, decltype(cleanup)> pipeline(
			gst_parse_launch("appsrc name=input ! pngdec ! videoconvert ! videoscale add-borders=true ! "
											 "video/x-raw,format=RGB,width=240,height=120,pixel-aspect-ratio=1/1 ! "
											 "pngenc snapshot=true compression-level=9 ! appsink name=output sync=false",
											 &error),
			cleanup);
	if (error || !pipeline)
	{
		std::string reason = error ? error->message : "missing pipeline";
		g_clear_error(&error);
		throw std::runtime_error("Cannot create scene image converter: " + reason);
	}
	auto unref = [](GstElement* element) { gst_object_unref(element); };
	std::unique_ptr<GstElement, decltype(unref)> input(gst_bin_get_by_name(GST_BIN(pipeline.get()), "input"), unref);
	std::unique_ptr<GstElement, decltype(unref)> output(gst_bin_get_by_name(GST_BIN(pipeline.get()), "output"), unref);
	if (gst_element_set_state(pipeline.get(), GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
		throw std::runtime_error("Cannot start scene image converter");
	auto buffer = gst_buffer_new_allocate(nullptr, original.bytes.size(), nullptr);
	if (!buffer)
		throw std::runtime_error("Cannot allocate scene image buffer");
	gst_buffer_fill(buffer, 0, original.bytes.data(), original.bytes.size());
	if (gst_app_src_push_buffer(GST_APP_SRC(input.get()), buffer) != GST_FLOW_OK ||
			gst_app_src_end_of_stream(GST_APP_SRC(input.get())) != GST_FLOW_OK)
		throw std::runtime_error("Cannot submit scene image");
	auto sampleUnref = [](GstSample* sample) { gst_sample_unref(sample); };
	std::unique_ptr<GstSample, decltype(sampleUnref)> sample(
			gst_app_sink_try_pull_sample(GST_APP_SINK(output.get()), 5 * GST_SECOND), sampleUnref);
	if (!sample)
		throw std::runtime_error("Scene PNG conversion failed or timed out");
	GstMapInfo map;
	buffer = gst_sample_get_buffer(sample.get());
	if (!buffer || !gst_buffer_map(buffer, &map, GST_MAP_READ))
		throw std::runtime_error("Cannot read scene PNG");
	std::string bytes(reinterpret_cast<const char*>(map.data), map.size);
	gst_buffer_unmap(buffer, &map);
	return encodedImage(std::move(bytes));
}
const std::vector<std::pair<std::string, std::string>> Simple = {
		{"IncludeImage", "xs:string"}, {"PlateLocation", "xs:string"}, {"Country", "xs:string"},
		{"Sensitivity", "xs:float"},	 {"Threshold", "xs:float"},			 {"PlateMinWidth", "xs:float"},
		{"PlateMaxWidth", "xs:float"}, {"EventInterval", "xs:int"}};
bool text(const Tree& tree)
{
	return tree.data().find_first_not_of(" \t\r\n") != std::string::npos;
}
Tree geometryXml(const Tree& rule, const std::string& name)
{
	Tree xml;
	xml.put("<xmlattr>.Name", rule.get<std::string>("Name"));
	xml.put("<xmlattr>.Type", name == "Region" ? field::Type : line::Type);
	xml.put_child("tt:Parameters.tt:ElementItem", rule.get_child("tt:Parameters.tt:ElementItem"));
	xml.put("tt:Parameters.tt:ElementItem.<xmlattr>.Name", name == "Region" ? "Field" : "Segments");
	return xml;
}
} // namespace

Image PreviewImage(const Image& original) { return thumbnail(original); }

const std::array<Sample, 5>& TestVehicles()
{
	auto make = [](const char* plate, const char* country, const char* type, const char* brand, const char* model,
								 double speed, double width, double likelihood, size_t index, const char* location = "Front") {
		Sample sample;
		sample.plate = plate;
		sample.country = country;
		sample.location = location;
		sample.vehicleType = type;
		sample.vehicleBrand = brand;
		sample.vehicleModel = model;
		sample.speed = speed;
		sample.width = width;
		sample.likelihood = likelihood;
		sample.imageIndex = index;
 static constexpr std::array<int, 3> colors[] = {{0,80,200},{200,30,30},{240,240,240},{240,200,0},{30,140,60}};
 sample.vehicleColor = colors[index];
		return sample;
	};
	static const std::array<Sample, 5> vehicles{
			make("TEST123", "US", "Car", "Test Brand", "Synthetic Vehicle", 12.5, 10, 0.95, 0),
			make("TEST456", "GB", "Car", "Test Motors", "Red SUV", 8, 12, 0.92, 1),
			make("TEST789", "DE", "Truck", "Test Cargo", "White Van", 6, 14, 0.97, 2),
			make("TEST321", "FR", "Bus", "Test Transit", "Yellow Minibus", 10, 16, 0.90, 3),
			make("TEST654", "IT", "Motorcycle", "Test Riders", "Green Motorcycle", 15, 6, 0.94, 4, "Rear")};
	return vehicles;
}

Sample SyntheticSample(size_t step)
{
	constexpr double positions[] = {-0.75, -0.25, 0.25, 0.75, 0.75, 0.25, -0.25, -0.75};
	const auto& vehicles = TestVehicles();
	auto sample = vehicles[step % vehicles.size()];
	auto phase = (step / vehicles.size()) % std::size(positions);
	sample.x = positions[phase];
	auto previous = positions[(phase + std::size(positions) - 1) % std::size(positions)];
	sample.direction = sample.x > previous ? "Right" : sample.x < previous ? "Left" : "Any";
	if (sample.x == previous)
		sample.speed = 0;
	return sample;
}

std::string ImagePath(size_t index, bool plate)
{
	auto suffix = index == 0 ? std::string{} : "-" + TestVehicles().at(index).plate;
	return std::string("/analytics/lpr/") + (plate ? "plate" : "vehicle") + suffix + ".png";
}

std::string SceneImagePath(size_t index)
{
	auto path = ImagePath(index, true);
	path.insert(std::string("/analytics/lpr").size(), "/scene");
	return path;
}

bool ImagesReady(const Images& images)
{
	return !images.empty() && std::all_of(images.begin(), images.end(), [](const auto& pair) {
		return !pair.plate.base64.empty() && !pair.vehicle.base64.empty() && !pair.plateUri.empty() &&
					 !pair.vehicleUri.empty();
	});
}

Images LoadImages(const std::string& configsPath, const std::string& httpAddress)
{
	auto load = [&](const std::string& name) {
		auto path = configsPath + "/rs/" + name;
		std::ifstream file(path, std::ios::binary);
		if (!file)
			throw std::runtime_error("Cannot open LPR test image: " + path);
		Image result;
		result.bytes.assign(std::istreambuf_iterator<char>(file), {});
		if (file.bad() || result.bytes.empty())
			throw std::runtime_error("Cannot read LPR test image: " + path);
		return encodedImage(std::move(result.bytes));
	};
	Images result;
	auto base = httpAddress;
	if (base.ends_with('/'))
		base.pop_back();
	for (size_t index = 0; index < TestVehicles().size(); ++index)
	{
		auto suffix = index == 0 ? std::string{} : "-" + TestVehicles()[index].plate;
		ImagePair pair;
		pair.plate = load("lpr-plate-test" + suffix + ".png");
		pair.vehicle = load("lpr-test" + suffix + ".png");
		pair.scene = thumbnail(pair.plate);
		pair.plateUri = base + ImagePath(index, true);
		pair.vehicleUri = base + ImagePath(index, false);
		pair.sceneUri = base + SceneImagePath(index);
		result.push_back(std::move(pair));
	}
	return result;
}

Tree ReadRule(const Tree& xml, const Tree* previous)
{
	if (xml.get<std::string>("<xmlattr>.Type") != Type || xml.size() != 2 || xml.get_child("<xmlattr>").size() != 2 ||
			xml.count("tt:Parameters") != 1 || text(xml))
		throw RuleFault("InvalidRule");
	Tree result = previous ? *previous : Tree{};
	result.put("Name", xml.get<std::string>("<xmlattr>.Name"));
	result.put("Type", Type);
	if (result.get<std::string>("Name").empty())
		throw RuleFault("InvalidRule");
	if (!previous)
	{
		for (auto name : {"IncludeImage", "PlateLocation", "Country"})
			result.put(name, "");
		result.put("IncludeImage", "Embedded");
		result.put("Sensitivity", 0.5);
		result.put("Threshold", 0.5);
		result.put("PlateMinWidth", 0);
		result.put("PlateMaxWidth", 100);
		result.put("EventInterval", 0);
	}
	const auto& parameters = xml.get_child("tt:Parameters");
	if (text(parameters))
		throw RuleFault("InvalidRule");
	std::set<std::string> names;
	for (const auto& [key, item] : parameters)
	{
		auto name = item.get<std::string>("<xmlattr>.Name");
		if (!names.insert(name).second || text(item))
			throw RuleFault("InvalidRule");
		if (key == "tt:SimpleItem")
		{
			if (item.size() != 1 || item.get_child("<xmlattr>").size() != 2 ||
					std::none_of(Simple.begin(), Simple.end(), [&](const auto& p) { return p.first == name; }))
				throw RuleFault("InvalidRule");
			result.put(name, item.get<std::string>("<xmlattr>.Value"));
		}
		else if (key == "tt:ElementItem" && (name == "Region" || name == "SnapLine"))
		{
			if (item.size() != 2 || item.get_child("<xmlattr>").size() != 1)
				throw RuleFault("InvalidRule");
			Tree wrapper;
			wrapper.put("Name", result.get<std::string>("Name"));
			wrapper.put_child("tt:Parameters.tt:ElementItem", item);
			auto parsed = geometryXml(wrapper, name);
			result.put_child(name, name == "Region" ? field::ReadRule(parsed, nullptr).get_child("Field")
																							: line::ReadRule(parsed, nullptr).get_child("Segments"));
		}
		else
			throw RuleFault("InvalidRule");
	}
	auto imageMode = result.get<std::string>("IncludeImage");
	if (!imageMode.empty() && imageMode != "Embedded" && imageMode != "LocalStorage")
		throw RuleFault("InvalidRule");
	auto location = result.get<std::string>("PlateLocation");
	if (!location.empty() && location != "Front" && location != "Rear")
		throw RuleFault("InvalidRule");
	for (auto name : {"Sensitivity", "Threshold", "PlateMinWidth", "PlateMaxWidth"})
	{
		auto value = result.get<double>(name);
		if (!std::isfinite(value) || value < 0 || value > (std::string(name).starts_with("Plate") ? 100 : 1))
			throw RuleFault("InvalidRule");
	}
	if (result.get<double>("PlateMinWidth") > result.get<double>("PlateMaxWidth") || result.get<int>("EventInterval") < 0)
		throw RuleFault("InvalidRule");
	return result;
}

Tree RuleXml(const Tree& rule)
{
	Tree result, params;
	result.put("<xmlattr>.Name", rule.get<std::string>("Name"));
	result.put("<xmlattr>.Type", Type);
	for (const auto& [name, _] : Simple)
		if (auto value = rule.get_optional<std::string>(name))
		{
			Tree item;
			item.put("<xmlattr>.Name", name);
			item.put("<xmlattr>.Value", *value);
			params.add_child("tt:SimpleItem", item);
		}
	for (auto name : {"Region", "SnapLine"})
		if (auto points = rule.get_child_optional(name))
		{
			Tree geometry;
			for (const auto& [_, p] : *points)
			{
				Tree point;
				point.put("<xmlattr>.x", p.get<double>("x"));
				point.put("<xmlattr>.y", p.get<double>("y"));
				geometry.add_child("tt:Point", point);
			}
			Tree item;
			item.put("<xmlattr>.Name", name);
			item.add_child(std::string(name) == "Region" ? "tt:Polygon" : "tt:Polyline", geometry);
			params.add_child("tt:ElementItem", item);
		}
	result.add_child("tt:Parameters", params);
	return result;
}

Tree Description()
{
	Tree desc;
	desc.put("<xmlattr>.Name", Type);
	desc.put("<xmlattr>.fixed", false);
	desc.put("<xmlattr>.maxInstances", 4);
	auto add = [&](const std::string& path, const std::string& name, const std::string& type) {
		Tree item;
		item.put("<xmlattr>.Name", name);
		item.put("<xmlattr>.Type", type);
		desc.add_child(path, item);
	};
	for (const auto& [name, type] : Simple)
		add("tt:Parameters.tt:SimpleItemDescription", name, type);
	add("tt:Parameters.tt:ElementItemDescription", "Region", "tt:Polygon");
	add("tt:Parameters.tt:ElementItemDescription", "SnapLine", "tt:Polyline");
	desc.put("tt:Messages.<xmlattr>.IsProperty", false);
	add("tt:Messages.tt:Source.tt:SimpleItemDescription", "VideoSource", "tt:ReferenceToken");
	add("tt:Messages.tt:Source.tt:SimpleItemDescription", "Rule", "xs:string");
	add("tt:Messages.tt:Data.tt:SimpleItemDescription", "Likelihood", "xs:float");
	for (auto [name, type] : {std::pair{"ImageURI", "xs:anyURI"},
														{"VehicleImageURI", "xs:anyURI"},
														{"Speed", "xs:float"},
														{"Direction", "tt:Direction"}})
		add("tt:Messages.tt:Data.tt:SimpleItemDescription", name, type);
	for (auto [name, type] : {std::pair{"LicensePlateInfo", "tt:LicensePlateInfo"},
														{"BoundingBox", "tt:Rectangle"},
														{"VehicleInfo", "tt:VehicleInfo"},
														{"Image", "xs:base64Binary"},
														{"VehicleImage", "xs:base64Binary"}})
		add("tt:Messages.tt:Data.tt:ElementItemDescription", name, type);
	desc.put("tt:Messages.tt:ParentTopic", Topic);
	return desc;
}

Tree Options()
{
	Tree result;
	auto add = [&](const std::string& name, const std::string& type, const Tree& value) {
		Tree option;
		option.put("<xmlattr>.Name", name);
		option.put("<xmlattr>.RuleType", Type);
		option.put("<xmlattr>.Type", type);
		option.add_child(type, value);
		result.add_child("tan:RuleOptions", option);
	};
	for (const auto& [name, _] : Simple)
	{
		if (name == "Country")
			continue; // Arbitrary country strings are accepted; there is no finite option list.
		Tree value;
		if (name == "IncludeImage" || name == "PlateLocation" || name == "Country")
		{
			value.data() = name == "PlateLocation" ? "Front Rear" : "Embedded LocalStorage";
			add(name, "tt:StringList", value);
		}
		else
		{
			value.put("tt:Min", 0);
			value.put("tt:Max", name == "EventInterval" ? 2147483647 : name.starts_with("Plate") ? 100 : 1);
			add(name, name == "EventInterval" ? "tt:IntRange" : "tt:FloatRange", value);
		}
	}
	Tree polygon;
	polygon.put("tt:RectangleOnly", false);
	polygon.put("tt:VertexLimits.tt:Min", 3);
	polygon.put("tt:VertexLimits.tt:Max", 8);
	add("Region", "tt:PolygonOptions", polygon);
	Tree segments;
	segments.put("tt:Min", 2);
	segments.put("tt:Max", 8);
	add("SnapLine", "tt:IntRange", segments);
	return result;
}
} // namespace utility::lpr
