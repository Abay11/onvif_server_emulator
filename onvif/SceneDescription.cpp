#include "SceneDescription.h"
#include <algorithm>
#include <charconv>
#include <limits>
#include <stdexcept>

namespace osrv::scene
{
namespace
{
constexpr std::size_t MaxFrames = 128, MaxObjects = 64;
boost::property_tree::ptree ColorXml(const std::array<int, 3>& rgb)
{
	boost::property_tree::ptree color;
	color.put("tt:ColorCluster.tt:Color.<xmlattr>.X", rgb[0]);
	color.put("tt:ColorCluster.tt:Color.<xmlattr>.Y", rgb[1]);
	color.put("tt:ColorCluster.tt:Color.<xmlattr>.Z", rgb[2]);
	color.put("tt:ColorCluster.tt:Color.<xmlattr>.Colorspace", "http://www.onvif.org/ver10/colorspace/RGB");
	return color;
}
} // namespace
HumanFace SampleHumanFace()
{
	return {0, 0, 0, false, false};
}
HumanBody SampleHumanBody()
{
	return {175, "LongSleeve", "Shirt", "Trousers", "Jeans", {40, 100, 180}, {30, 40, 70}, "Walking"};
}
const std::vector<Module>& Modules()
{
	static const std::vector<Module> modules{
			{ModuleName, ModuleType}, {LprModule, "osrv:LicensePlateRecognition"}, {FaceModule, "osrv:FaceRecognition"}};
	return modules;
}
std::string Buffer::Sample(const std::string& source, const std::vector<std::string>& configurations,
													 const std::string& input_id, double x, double y, const std::string& utc_time,
													 const std::string& classification, std::optional<LicensePlate> plate, ImagePtr image,
													 const std::string& module, std::optional<Vehicle> vehicle, const std::string& parent,
													 std::optional<HumanFace> face, std::optional<HumanBody> body)
{
	std::lock_guard lock(mutex_);
	auto& state = sources_[{source, module}];
	auto& next_id = next_ids_[source];
	if (!next_id)
		next_id = 1;
	auto found = state.objects.find(input_id);
	std::vector<std::string> deleted;
	if (found == state.objects.end())
	{
		if (state.objects.size() == MaxObjects)
		{
			deleted.push_back(state.objects.begin()->second.id);
			state.objects.erase(state.objects.begin());
		}
		std::uint64_t candidate = 0;
		auto parsed = std::from_chars(input_id.data(), input_id.data() + input_id.size(), candidate);
		if (parsed.ec == std::errc{} && parsed.ptr == input_id.data() + input_id.size() && candidate >= next_id &&
				candidate < std::numeric_limits<std::uint64_t>::max())
			next_id = candidate;
		if (next_id == std::numeric_limits<std::uint64_t>::max())
			throw std::overflow_error("Synthetic scene ObjectId space exhausted");
		found = state.objects.emplace(input_id, Object{std::to_string(next_id++), x, y, classification}).first;
	}
	found->second.x = x;
	found->second.y = y;
	found->second.classification = classification;
	found->second.plate = std::move(plate);
	found->second.image = std::move(image);
	found->second.vehicle = std::move(vehicle);
	found->second.parent = parent;
	found->second.face = std::move(face);
	found->second.body = std::move(body);
	Publish(source, configurations, utc_time, deleted, found->second.image ? found->second.id : std::string{}, module);
	return found->second.id;
}
void Buffer::Forget(const std::string& source, const std::vector<std::string>& configurations,
										const std::string& input_id, const std::string& utc_time)
{
	std::lock_guard lock(mutex_);
	auto src = sources_.find({source, ModuleName});
	if (src == sources_.end())
		return;
	auto object = src->second.objects.find(input_id);
	if (object == src->second.objects.end())
		return;
	auto id = object->second.id;
	src->second.objects.erase(object);
	Publish(source, configurations, utc_time, {id});
}
void Buffer::Publish(const std::string& source, const std::vector<std::string>& configurations,
										 const std::string& utc_time, const std::vector<std::string>& deleted,
										 const std::string& image_object_id, const std::string& module)
{
	for (const auto& configuration : configurations)
	{
		auto frame = std::make_shared<Frame>();
		frame->sequence = ++sequence_;
		frame->video_source = source;
		frame->configuration = configuration;
		frame->utc_time = utc_time;
		frame->image_object_id = image_object_id;
		frame->module = module;
		frame->deleted = deleted;
		for (const auto& [_, object] : sources_.at({source, module}).objects)
			frame->objects.push_back(object);
		latest_[{source, configuration, module}] = frame;
		frames_.push_back(std::move(frame));
		if (frames_.size() > MaxFrames)
			frames_.pop_front();
	}
}
Batch Buffer::Read(const std::string& source, const std::string& configuration, std::uint64_t after,
									 std::size_t limit) const
{
	std::lock_guard lock(mutex_);
	Batch result;
	if (!limit || frames_.empty() || source.empty() || configuration.empty())
		return result;
	bool latest = !after || after < frames_.front()->sequence - 1;
	result.skipped = after && latest;
	if (latest)
	{
		for (const auto& [key, frame] : latest_)
			if (frame->video_source == source && frame->configuration == configuration && frame->sequence > after)
				result.frames.push_back(frame);
		std::sort(result.frames.begin(), result.frames.end(),
							[](const auto& a, const auto& b) { return a->sequence < b->sequence; });
		if (result.frames.size() > limit)
			result.frames.resize(limit);
	}
	else
		for (const auto& frame : frames_)
			if (frame->video_source == source && frame->configuration == configuration && frame->sequence > after)
			{
				result.frames.push_back(frame);
				if (result.frames.size() == limit)
					break;
			}
	return result;
}
boost::property_tree::ptree FrameXml(const Frame& frame)
{
	boost::property_tree::ptree out;
	out.put("<xmlattr>.UtcTime", frame.utc_time);
	out.put("<xmlattr>.Source", frame.module);
	out.put("tt:Transformation.tt:Translate.<xmlattr>.x", 0);
	out.put("tt:Transformation.tt:Translate.<xmlattr>.y", 0);
	out.put("tt:Transformation.tt:Scale.<xmlattr>.x", 1);
	out.put("tt:Transformation.tt:Scale.<xmlattr>.y", 1);
	for (const auto& object : frame.objects)
	{
		boost::property_tree::ptree node;
		node.put("<xmlattr>.ObjectId", object.id);
		if (!object.parent.empty())
			node.put("<xmlattr>.Parent", object.parent);
		auto shape = std::string("tt:Appearance.tt:Shape.");
		auto halfWidth = object.plate ? object.plate->width / 100
																	: (object.vehicle													? 0.25
																		 : object.classification == "HumanFace" ? 0.45
																																						: 0.08);
		auto halfHeight = object.plate ? object.plate->height / 100
																	 : (object.vehicle												 ? 0.3
																			: object.classification == "HumanFace" ? 0.525
																																						 : 0.18);
		node.put(shape + "tt:BoundingBox.<xmlattr>.left", std::max(-1.0, object.x - halfWidth));
		node.put(shape + "tt:BoundingBox.<xmlattr>.right", std::min(1.0, object.x + halfWidth));
		node.put(shape + "tt:BoundingBox.<xmlattr>.bottom", std::max(-1.0, object.y - halfHeight));
		node.put(shape + "tt:BoundingBox.<xmlattr>.top", std::min(1.0, object.y + halfHeight));
		node.put(shape + "tt:CenterOfGravity.<xmlattr>.x", object.x);
		node.put(shape + "tt:CenterOfGravity.<xmlattr>.y", object.y);
		if (object.vehicle)
		{
			const auto& vehicle = *object.vehicle;
			const std::string color = "tt:Appearance.tt:Color.tt:ColorCluster.tt:Color.<xmlattr>.";
			node.put(color + "X", vehicle.red);
			node.put(color + "Y", vehicle.green);
			node.put(color + "Z", vehicle.blue);
			node.put(color + "Colorspace", "http://www.onvif.org/ver10/colorspace/RGB");
		}
		if (!object.classification.empty())
		{
			node.put("tt:Appearance.tt:Class.tt:Type", object.classification);
			node.put("tt:Appearance.tt:Class.tt:Type.<xmlattr>.Likelihood", 1);
		}
		if (object.vehicle)
		{
			node.put("tt:Appearance.tt:VehicleInfo.tt:Type", object.vehicle->type);
			node.put("tt:Appearance.tt:VehicleInfo.tt:Brand", object.vehicle->brand);
			node.put("tt:Appearance.tt:VehicleInfo.tt:Model", object.vehicle->model);
		}
		if (object.plate)
		{
			const auto& plate = *object.plate;
			node.put("tt:Appearance.tt:LicensePlateInfo.tt:PlateNumber", plate.number);
			node.put("tt:Appearance.tt:LicensePlateInfo.tt:PlateNumber.<xmlattr>.Likelihood", plate.likelihood);
			if (!plate.country.empty())
				node.put("tt:Appearance.tt:LicensePlateInfo.tt:CountryCode", plate.country);
		}
		if (object.face)
		{
			boost::property_tree::ptree face;
			face.put("<xmlattr>.xmlns:fc", "http://www.onvif.org/ver20/analytics/humanface");
			face.put("fc:PoseAngle.fc:PoseAngles.<xmlattr>.yaw", object.face->yaw);
			face.put("fc:PoseAngle.fc:PoseAngles.<xmlattr>.pitch", object.face->pitch);
			face.put("fc:PoseAngle.fc:PoseAngles.<xmlattr>.roll", object.face->roll);
			face.put("fc:Accessory.fc:Opticals.fc:Wear", object.face->opticals);
			face.put("fc:Accessory.fc:Mask.fc:Wear", object.face->mask);
			node.add_child("tt:Appearance.tt:HumanFace", face);
		}
		if (object.body)
		{
			const auto& body = *object.body;
			boost::property_tree::ptree xml;
			xml.put("<xmlattr>.xmlns:bd", "http://www.onvif.org/ver20/analytics/humanbody");
			xml.put("bd:BodyMetric.bd:Height", body.height);
			xml.put("bd:Clothing.bd:Tops.bd:Category", body.tops_category);
			xml.add_child("bd:Clothing.bd:Tops.bd:Color", ColorXml(body.tops_color));
			xml.put("bd:Clothing.bd:Tops.bd:Style", body.tops_style);
			xml.put("bd:Clothing.bd:Bottoms.bd:Category", body.bottoms_category);
			xml.add_child("bd:Clothing.bd:Bottoms.bd:Color", ColorXml(body.bottoms_color));
			xml.put("bd:Clothing.bd:Bottoms.bd:Style", body.bottoms_style);
			xml.put("bd:Behaviour.bd:Activity", body.activity);
			node.add_child("tt:Appearance.tt:HumanBody", xml);
		}
		if (object.image)
		{
			if (!object.image->uri.empty())
				node.put("tt:Appearance.tt:ImageRef", object.image->uri);
			if (object.id == frame.image_object_id && !object.image->base64.empty())
				node.put("tt:Appearance.tt:Image", object.image->base64);
		}
		if (object.vehicle)
			node.put("tt:Behaviour.tt:Speed", object.vehicle->speed);
		out.add_child("tt:Object", node);
	}
	for (const auto& id : frame.deleted)
	{
		boost::property_tree::ptree node;
		node.put("<xmlattr>.ObjectId", id);
		out.add_child("tt:ObjectTree.tt:Delete", node);
	}
	return out;
}
boost::property_tree::ptree ModuleXml(const std::string& name)
{
	boost::property_tree::ptree module;
	module.put("<xmlattr>.Name", name);
	for (const auto& entry : Modules())
		if (entry.name == name)
			module.put("<xmlattr>.Type", entry.type);
	module.put("<xmlattr>.xmlns:osrv", ModuleNamespace);
	module.put("tt:Parameters", "");
	return module;
}
Frame SampleFrame(ImagePtr image)
{
	Frame frame;
	frame.utc_time = "2000-01-01T00:00:00Z";
	frame.image_object_id = "3";
	frame.objects.push_back({"1", -0.6, 0.6, "Human"});
	frame.objects.back().body = SampleHumanBody();
	frame.objects.push_back({"3", -0.3, 0.6, "LicensePlate", LicensePlate{"TEST123", "US", 0.95}, std::move(image)});
	frame.deleted.push_back("2");
	return frame;
}
} // namespace osrv::scene
