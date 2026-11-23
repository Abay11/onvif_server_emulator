#include "FaceRecognition.h"
#include "AnalyticsRules.h"
#include <algorithm>
#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/transform_width.hpp>
#include <cmath>
#include <fstream>
#include <iterator>
#include <set>

namespace utility::face
{
std::string ImagePath(size_t index)
{
	return "/analytics/face/face-00" + std::to_string(index + 1) + ".png";
}

std::string SceneImagePath(size_t index)
{
 return "/analytics/face/scene/face-00" + std::to_string(index + 1) + ".png";
}
Images LoadImages(const std::string& configsPath, const std::string& httpAddress)
{
	Images result;
	auto base = httpAddress;
	if (base.ends_with('/'))
		base.pop_back();
	for (size_t index = 0; index < 3; ++index)
	{
		auto path = configsPath + "/rs/face-test-00" + std::to_string(index + 1) + ".png";
		std::ifstream file(path, std::ios::binary);
		if (!file)
			throw std::runtime_error("Cannot open face test image: " + path);
		Image image;
		image.image.bytes.assign(std::istreambuf_iterator<char>(file), {});
		if (file.bad() || image.image.bytes.empty())
			throw std::runtime_error("Cannot read face test image: " + path);
		using namespace boost::archive::iterators;
		using Encoder = base64_from_binary<transform_width<std::string::const_iterator, 6, 8>>;
		image.image.base64.assign(Encoder(image.image.bytes.begin()), Encoder(image.image.bytes.end()));
		image.image.base64.append((3 - image.image.bytes.size() % 3) % 3, '=');
		image.uri = base + ImagePath(index);
 image.preview = lpr::PreviewImage(image.image);
 image.previewUri = base + SceneImagePath(index);
		result.push_back(std::move(image));
	}
	return result;
}

bool ImagesReady(const Images& images)
{
	return images.size() == 3 && std::all_of(images.begin(), images.end(), [](const auto& image) {
					 return !image.image.bytes.empty() && !image.image.base64.empty() && !image.uri.empty();
				 });
}

Tree ReadRule(const Tree& xml, const Tree* previous)
{
	auto hasText = [](const Tree& value) { return value.data().find_first_not_of(" \t\r\n") != std::string::npos; };
	if (xml.get<std::string>("<xmlattr>.Type") != Type || xml.size() != 2 || xml.get_child("<xmlattr>").size() != 2 ||
			xml.count("tt:Parameters") != 1 || hasText(xml))
		throw RuleFault("InvalidRule");
	Tree result = previous ? *previous : Tree{};
	result.put("Name", xml.get<std::string>("<xmlattr>.Name"));
	result.put("Type", Type);
	if (result.get<std::string>("Name").empty())
		throw RuleFault("InvalidRule");
	if (!previous)
	{
		result.put("IncludeImage", "Embedded");
		result.put("Threshold", 0.5);
	}
	const auto& parameters = xml.get_child("tt:Parameters");
	if (hasText(parameters))
		throw RuleFault("InvalidRule");
	std::set<std::string> names;
	for (const auto& [key, item] : parameters)
	{
		auto name = item.get<std::string>("<xmlattr>.Name");
		if (key != "tt:SimpleItem" || (name != "IncludeImage" && name != "Threshold") || !names.insert(name).second ||
				hasText(item) || item.size() != 1 || item.get_child("<xmlattr>").size() != 2)
			throw RuleFault("InvalidRule");
		result.put(name, item.get<std::string>("<xmlattr>.Value"));
	}
	auto mode = result.get<std::string>("IncludeImage");
	auto threshold = result.get<double>("Threshold");
	if ((!mode.empty() && mode != "Embedded" && mode != "LocalStorage") || !std::isfinite(threshold) || threshold < 0 ||
			threshold > 1)
		throw RuleFault("InvalidRule");
	return result;
}

Tree RuleXml(const Tree& rule)
{
	Tree result;
	result.put("<xmlattr>.Name", rule.get<std::string>("Name"));
	result.put("<xmlattr>.Type", Type);
	result.put("tt:Parameters", "");
	for (auto name : {"IncludeImage", "Threshold"})
		if (auto value = rule.get_optional<std::string>(name))
		{
			Tree item;
			item.put("<xmlattr>.Name", name);
			item.put("<xmlattr>.Value", *value);
			result.add_child("tt:Parameters.tt:SimpleItem", item);
		}
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
	add("tt:Parameters.tt:SimpleItemDescription", "IncludeImage", "xs:string");
	add("tt:Parameters.tt:SimpleItemDescription", "Threshold", "xs:float");
	desc.put("tt:Messages.<xmlattr>.IsProperty", false);
	add("tt:Messages.tt:Source.tt:SimpleItemDescription", "VideoSource", "tt:ReferenceToken");
	add("tt:Messages.tt:Source.tt:SimpleItemDescription", "Rule", "xs:string");
	for (auto [name, type] : {std::pair{"Likelihood", "xs:float"},
														{"Label", "xs:string"},
														{"ImageUri", "xs:anyURI"},
														{"EnrollmentID", "xs:string"},
														{"RefImageUri", "xs:anyURI"}})
		add("tt:Messages.tt:Data.tt:SimpleItemDescription", name, type);
	for (auto [name, type] :
			 {std::pair{"ObjectId", "tt:ObjectId"}, {"Image", "xs:base64Binary"}, {"BoundingBox", "tt:Rectangle"}})
		add("tt:Messages.tt:Data.tt:ElementItemDescription", name, type);
	desc.put("tt:Messages.tt:ParentTopic", Topic);
	return desc;
}

Tree Options()
{
	Tree result, image, threshold;
	image.put("<xmlattr>.Name", "IncludeImage");
	image.put("<xmlattr>.RuleType", Type);
	image.put("<xmlattr>.Type", "tt:StringList");
	image.put("tt:StringList", "Embedded LocalStorage");
	result.add_child("tan:RuleOptions", image);
	threshold.put("<xmlattr>.Name", "Threshold");
	threshold.put("<xmlattr>.RuleType", Type);
	threshold.put("<xmlattr>.Type", "tt:FloatRange");
	threshold.put("tt:FloatRange.tt:Min", 0);
	threshold.put("tt:FloatRange.tt:Max", 1);
	result.add_child("tan:RuleOptions", threshold);
	return result;
}
} // namespace utility::face
