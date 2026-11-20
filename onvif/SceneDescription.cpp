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
}
std::string Buffer::Sample(const std::string& source, const std::vector<std::string>& configurations,
													 const std::string& input_id, double x, double y, const std::string& utc_time,
													 const std::string& classification)
{
	std::lock_guard lock(mutex_);
	auto& state = sources_[source];
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
		if (parsed.ec == std::errc{} && parsed.ptr == input_id.data() + input_id.size() && candidate >= state.next_id &&
				candidate < std::numeric_limits<std::uint64_t>::max())
			state.next_id = candidate;
		if (state.next_id == std::numeric_limits<std::uint64_t>::max())
			throw std::overflow_error("Synthetic scene ObjectId space exhausted");
		found = state.objects.emplace(input_id, Object{std::to_string(state.next_id++), x, y, classification}).first;
	}
	found->second.x = x;
	found->second.y = y;
	found->second.classification = classification;
	Publish(source, configurations, utc_time, deleted);
	return found->second.id;
}
void Buffer::Forget(const std::string& source, const std::vector<std::string>& configurations,
										const std::string& input_id, const std::string& utc_time)
{
	std::lock_guard lock(mutex_);
	auto src = sources_.find(source);
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
										 const std::string& utc_time, const std::vector<std::string>& deleted)
{
	for (const auto& configuration : configurations)
	{
		auto frame = std::make_shared<Frame>();
		frame->sequence = ++sequence_;
		frame->video_source = source;
		frame->configuration = configuration;
		frame->utc_time = utc_time;
		frame->deleted = deleted;
		for (const auto& [_, object] : sources_.at(source).objects)
			frame->objects.push_back(object);
		latest_[{source, configuration}] = frame;
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
	auto current = latest_.find({source, configuration});
	if (current == latest_.end() || (after && current->second->sequence <= after))
		return result;
	bool latest = !after || after < frames_.front()->sequence - 1;
	result.skipped = after && latest;
	if (latest)
		result.frames.push_back(current->second);
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
	out.put("<xmlattr>.Source", ModuleName);
	out.put("tt:Transformation.tt:Translate.<xmlattr>.x", 0);
	out.put("tt:Transformation.tt:Translate.<xmlattr>.y", 0);
	out.put("tt:Transformation.tt:Scale.<xmlattr>.x", 1);
	out.put("tt:Transformation.tt:Scale.<xmlattr>.y", 1);
	for (const auto& object : frame.objects)
	{
		boost::property_tree::ptree node;
		node.put("<xmlattr>.ObjectId", object.id);
		auto shape = std::string("tt:Appearance.tt:Shape.");
		node.put(shape + "tt:BoundingBox.<xmlattr>.left", std::max(-1.0, object.x - 0.08));
		node.put(shape + "tt:BoundingBox.<xmlattr>.right", std::min(1.0, object.x + 0.08));
		node.put(shape + "tt:BoundingBox.<xmlattr>.bottom", std::max(-1.0, object.y - 0.18));
		node.put(shape + "tt:BoundingBox.<xmlattr>.top", std::min(1.0, object.y + 0.18));
		node.put(shape + "tt:CenterOfGravity.<xmlattr>.x", object.x);
		node.put(shape + "tt:CenterOfGravity.<xmlattr>.y", object.y);
		if (!object.classification.empty())
		{
			node.put("tt:Appearance.tt:Class.tt:Type", object.classification);
			node.put("tt:Appearance.tt:Class.tt:Type.<xmlattr>.Likelihood", 1);
		}
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
boost::property_tree::ptree ModuleXml()
{
	boost::property_tree::ptree module;
	module.put("<xmlattr>.Name", ModuleName);
	module.put("<xmlattr>.Type", ModuleType);
	module.put("<xmlattr>.xmlns:osrv", ModuleNamespace);
	module.put("tt:Parameters", "");
	return module;
}
Frame SampleFrame()
{
	Frame frame;
	frame.utc_time = "2000-01-01T00:00:00Z";
	frame.objects.push_back({"1", -0.6, 0.6, "Human"});
	frame.deleted.push_back("2");
	return frame;
}
} // namespace osrv::scene
