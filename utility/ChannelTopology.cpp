#include "ChannelTopology.h"
#include "ConfigValidation.h"
#include "MediaProfilesManager.h"

#include <boost/property_tree/json_parser.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>

namespace utility::media
{
namespace
{
using Tree = boost::property_tree::ptree;

[[noreturn]] void Invalid(const std::string& catalog, const std::string& token)
{
	throw std::invalid_argument("Invalid channel topology reference in " + catalog + ": " + token);
}

auto FindToken(auto& list, std::string_view token, const char* field)
{
	return std::ranges::find_if(
			list, [&](const auto& entry) { return entry.second.template get<std::string>(field, "") == token; });
}

Tree& Catalog(Tree& document, const std::string& channel, const std::string& type)
{
	if (channel.empty() && type == "MediaProfiles")
	{
		if (!document.get_child_optional("unassignedProfiles"))
			document.put_child("unassignedProfiles", {});
		return document.get_child("unassignedProfiles");
	}
	auto& channels = document.get_child("channels");
	const auto owner = FindToken(channels, channel, "token");
	if (owner == channels.end())
		throw osrv::configuration_conflict();
	auto& media = owner->second.get_child("media");
	if (!media.get_child_optional(type))
		media.put_child(type, {});
	return media.get_child(type);
}

} // namespace

ChannelTopology::ChannelTopology(const Tree& document) : channels_(document.get_child("channels"))
{
	if (channels_.empty() || channels_.size() > 32)
		throw std::invalid_argument("Channel count must be between 1 and 32");

	for (const auto& [_, channel] : channels_)
	{
		const auto channelToken = channel.get<std::string>("token");
		auto index = [&](const std::string& catalog, const std::string& token) {
			if (token.empty() || !owners_[catalog].emplace(token, channelToken).second)
				Invalid(catalog, token);
		};

		index("channels", channelToken);
		const auto videoSource = channel.get<std::string>("videoSource.token");
		index("VideoSources", videoSource);
		const auto& catalog = channel.get_child("media");
		for (const auto& [type, items] : catalog)
		{
			if (!media_.get_child_optional(type))
				media_.put_child(type, {});
			for (const auto& [unused, item] : items)
			{
				const auto token = item.get_optional<std::string>(type == "AudioSources" ? "Token" : "token");
				if (token)
					index(type, *token);
				auto& projected = media_.get_child(type).push_back({"", item})->second;
				if (type == "MediaProfiles")
					projected.put("channel", channelToken);
			}
		}

		const Tree empty;
		for (const auto& [unused, config] : catalog.get_child("PTZ"))
		{
			const auto node = config.get<std::string>("NodeToken");
			const auto owner = Owner("PTZNodes", node);
			if (owner.empty())
				index("PTZNodes", node);
			else if (owner != channelToken)
				Invalid("PTZ.NodeToken", node);
		}
		for (const auto& [unused, source] : catalog.get_child("VideoSource"))
		{
			if (source.get<std::string>("SourceToken") != videoSource)
				Invalid("VideoSource.SourceToken", source.get<std::string>("SourceToken"));
			for (const auto& [entryKey, item] : source.get_child("CompatiblePtzNodes", empty))
				if (Owner("PTZNodes", item.data()) != channelToken)
					Invalid("CompatiblePtzNodes", item.data());
			for (const auto& [entryKey, item] : source.get_child("CompatibleAnalyticsConfigurations", empty))
				if (Owner("Analytics", item.data()) != channelToken)
					Invalid("CompatibleAnalyticsConfigurations", item.data());
		}
		for (const auto& [unused, source] : catalog.get_child("AudioSource"))
		{
			const auto token = source.get<std::string>("SourceToken");
			if (Owner("AudioSources", token) != channelToken)
				Invalid("AudioSource.SourceToken", token);
		}
		for (const auto& [unused, profile] : catalog.get_child("MediaProfiles"))
		{
			for (const auto& type : osrv::CONFIGURATION_ENUMERATION)
			{
				const auto token = profile.get<std::string>(type, "");
				if (!token.empty() && Owner(type, token) != channelToken)
					Invalid(type, token);
			}
			if (auto owner = profile.get_optional<std::string>("channel"); owner && *owner != channelToken)
				Invalid("MediaProfiles.channel", *owner);
		}
		for (const auto& [unused, encoder] : catalog.get_child("VideoEncoder"))
		{
			const auto token = encoder.get<std::string>("token");
			const auto& legacy = catalog.get_child("VideoEncoderConfigurations");
			const auto match = FindToken(legacy, token, "token");
			if (match == legacy.end())
				Invalid("VideoEncoderConfigurations", token);
			const auto width = encoder.get<int>("Resolution.Width");
			const auto height = encoder.get<int>("Resolution.Height");
			const auto rate = encoder.get<double>("RateControl.FrameRateLimit");
			const auto bitrate = encoder.get<int>("RateControl.BitrateLimit");
			const auto gop = encoder.get<int>("GovLength");
			const auto quality = encoder.get<double>("Quality");
			const auto profile = encoder.get<std::string>("Profile");
			if (encoder.get<std::string>("Encoding") != "H264" ||
					(profile != "Baseline" && profile != "Main" && profile != "High") || width < 16 || height < 16 ||
					width > 4096 || height > 4096 || width % 2 || height % 2 || rate < 1 || rate > 120 ||
					std::floor(rate) != rate || bitrate < 1 || bitrate > 2048000 || gop < 1 || gop > 250 || quality < 0 ||
					quality > 10)
				Invalid("H264 settings", token);
			const auto& other = match->second;
			for (const auto* field : {"Name", "Encoding", "Resolution.Width", "Resolution.Height", "GuaranteedFrameRate"})
				if (encoder.get<std::string>(field, "") != other.get<std::string>(field, ""))
					Invalid("VideoEncoderConfigurations", token);
			for (const auto* field : {"RateControl.FrameRateLimit", "RateControl.BitrateLimit", "Quality"})
				if (encoder.get<double>(field, 0) != other.get<double>(field, 0))
					Invalid("VideoEncoderConfigurations", token);
			if (encoder.get<std::string>("Profile") != other.get<std::string>("H264.H264Profile") ||
					encoder.get<int>("GovLength") != other.get<int>("H264.GovLength") ||
					other.get<int>("RateControl.EncodingInterval") != 1)
				Invalid("H264", token);
		}
		for (const auto& [unused, encoder] : catalog.get_child("VideoEncoderConfigurations"))
		{
			const auto token = encoder.get<std::string>("token");
			if (Owner("VideoEncoder", token) != channelToken)
				Invalid("VideoEncoder", token);
		}
	}
	for (const auto* type : {"Analytics", "Metadata"})
		if (!media_.get_child_optional(type))
			media_.put_child(type, {});
	if (const auto unassigned = document.get_child_optional("unassignedProfiles"))
		for (const auto& [_, profile] : *unassigned)
		{
			const auto token = profile.get<std::string>("token");
			if (profile.get_optional<std::string>("channel") || profile.get<bool>("fixed") || token.empty() ||
					!owners_["MediaProfiles"].emplace(token, "").second)
				Invalid("unassignedProfiles", token);
			for (const auto& type : osrv::CONFIGURATION_ENUMERATION)
				if (!profile.get<std::string>(type, "").empty())
					Invalid("unassignedProfiles", token);
			media_.get_child("MediaProfiles").push_back({"", profile});
		}
}

ChannelTopology ChannelTopology::Load(const std::string& path)
{
	std::ifstream stream(path);
	if (!stream)
		throw std::runtime_error("Cannot open channel topology: " + path);
	const std::string bytes(std::istreambuf_iterator<char>(stream), {});
	utility::ValidateConfig("channels.config", bytes);
	std::istringstream input(bytes);
	Tree document;
	boost::property_tree::read_json(input, document);
	return ChannelTopology(document);
}

void ChannelTopology::SaveMedia(const std::string& path, const Tree& previous, const Tree& next) const
{
	ConfigsReaderWriter writer(path);
	writer.Read();
	auto& document = writer.ConfigsTree();

	std::set<std::string> types;
	for (const auto* media : {&previous, &next})
		for (const auto& [type, _] : *media)
			types.insert(type);

	const Tree empty;
	for (const auto& type : types)
	{
		const auto& before = previous.get_child(type, empty);
		const auto& after = next.get_child(type, empty);
		if (before == after)
			continue;
		const auto* field = type == "AudioSources" ? "Token" : "token";
		std::set<std::string> tokens;
		for (const auto* list : {&before, &after})
			for (const auto& [_, item] : *list)
			{
				const auto token = item.get_optional<std::string>(field);
				if (!token)
					throw osrv::configuration_conflict();
				tokens.insert(*token);
			}
		for (const auto& token : tokens)
		{
			const auto old = FindToken(before, token, field);
			const auto update = FindToken(after, token, field);
			const bool existed = old != before.end(), exists = update != after.end();
			if (existed && exists && old->second == update->second)
				continue;
			const std::string oldOwner = type != "MediaProfiles" ? std::string(Owner(type, token))
																	 : existed							 ? old->second.get<std::string>("channel", "")
																													 : "";
			const std::string newOwner =
					type == "MediaProfiles" && exists ? update->second.get<std::string>("channel", "") : oldOwner;
			if (!oldOwner.empty() && newOwner != oldOwner)
				throw osrv::configuration_conflict();
			if (existed)
			{
				auto& catalog = Catalog(document, oldOwner, type);
				const auto stored = FindToken(catalog, token, field);
				auto expected = old->second;
				expected.erase("channel");
				if (stored == catalog.end() || stored->second != expected)
					throw osrv::configuration_conflict();
				if (exists && oldOwner == newOwner)
				{
					stored->second = update->second;
					stored->second.erase("channel");
					continue;
				}
				catalog.erase(stored);
			}
			if (exists)
			{
				auto& catalog = Catalog(document, newOwner, type);
				if (FindToken(catalog, token, field) != catalog.end())
					throw osrv::configuration_conflict();
				auto value = update->second;
				value.erase("channel");
				catalog.push_back({"", value});
			}
		}
	}
	static_cast<void>(ChannelTopology(document));
	writer.BeforeSave([](const std::string& bytes) { utility::ValidateConfig("channels.config", bytes); });
	writer.SaveJson(ChannelConfigJson(document));
}

std::string_view ChannelTopology::Owner(std::string_view catalog, std::string_view token) const
{
	const auto group = owners_.find(catalog);
	if (group == owners_.end())
		return {};
	const auto entry = group->second.find(token);
	return entry == group->second.end() ? std::string_view{} : std::string_view(entry->second);
}
} // namespace utility::media
