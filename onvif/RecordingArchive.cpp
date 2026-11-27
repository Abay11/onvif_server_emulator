#include "RecordingArchive.h"
#include "../utility/DateTime.hpp"
#include "../utility/MediaProfilesManager.h"

#include <boost/property_tree/json_parser.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <algorithm>
#include <filesystem>
#include <sstream>

namespace osrv
{
namespace
{
using Tree = RecordingArchive::Tree;
using Time = RecordingArchive::Time;
using Micros = std::chrono::microseconds;

long long Ticks(Time time)
{
	return std::chrono::duration_cast<Micros>(time.time_since_epoch()).count();
}

std::string Token()
{
	return boost::uuids::to_string(boost::uuids::random_generator()());
}
} // namespace

std::string RecordingUtc(Time time)
{
	return utility::datetime::posix_datetime_to_utc(boost::posix_time::ptime(boost::gregorian::date(1970, 1, 1)) +
																									boost::posix_time::microseconds(Ticks(time)));
}

std::vector<std::string> RecordingTracks(const RecordingArchive::Source& source)
{
	std::vector<std::string> tracks{"Video"};
	if (source.audio)
		tracks.emplace_back("Audio");
	if (source.metadata)
		tracks.emplace_back("Metadata");
	return tracks;
}

Tree RecordingSourceXml(const RecordingArchive::Source& source)
{
	Tree result;
	result.put("tt:SourceId", "urn:onvif-emulator:channel:" + source.channel);
	result.put("tt:Name", source.name);
	result.put("tt:Location", "");
	result.put("tt:Description", "Synthetic channel " + source.name);
	result.put("tt:Address", source.address);
	result.put("tt:VideoSourceToken", source.videoSource);
	return result;
}

RecordingArchive::RecordingArchive(std::string path, Clock clock) : path_(std::move(path)), clock_(std::move(clock))
{
	if (!std::filesystem::exists(path_))
		return;
	Tree document;
	boost::property_tree::read_json(path_, document);
	for (const auto& [channel, node] : document.get_child("recordings"))
	{
		Entry entry;
		entry.token = node.get<std::string>("token");
		entry.jobToken = node.get<std::string>("job");
		entry.active = node.get<bool>("active");
		entry.source.channel = channel;
		entry.source.profile = node.get<std::string>("source.profile");
		entry.source.videoSource = node.get<std::string>("source.videoSource");
		entry.source.name = node.get<std::string>("source.name");
		entry.source.address = node.get<std::string>("source.address");
		entry.source.video = node.get_child("source.video");
		if (auto audio = node.get_child_optional("source.audio"))
			entry.source.audio = *audio;
		entry.source.metadata = node.get<bool>("source.metadata", false);
		for (const auto& [_, value] : node.get_child("intervals"))
		{
			Interval interval{Time{Micros{value.get<long long>("start")}}, {}};
			if (auto end = value.get_optional<long long>("end"))
				interval.end = Time{Micros{*end}};
			else
				interval.end = interval.start;
			entry.intervals.push_back(interval);
		}
		if (entry.token.empty() || entry.jobToken.empty() || entry.intervals.empty())
			throw std::runtime_error("Recording archive contains an invalid recording");
		if (!entries_.emplace(channel, std::move(entry)).second)
			throw std::runtime_error("Duplicate channel in recording archive");
	}
}

std::vector<RecordingArchive::Entry> RecordingArchive::Snapshot() const
{
	std::lock_guard lock(mutex_);
	std::vector<Entry> result;
	for (const auto& [_, entry] : entries_)
		result.push_back(entry);
	return result;
}

std::optional<RecordingArchive::Entry> RecordingArchive::Recording(std::string_view token) const
{
	std::lock_guard lock(mutex_);
	const auto found = std::ranges::find_if(entries_, [&](const auto& pair) { return pair.second.token == token; });
	return found == entries_.end() ? std::nullopt : std::optional{found->second};
}

std::optional<RecordingArchive::Entry> RecordingArchive::Job(std::string_view token) const
{
	std::lock_guard lock(mutex_);
	const auto found = std::ranges::find_if(entries_, [&](const auto& pair) { return pair.second.jobToken == token; });
	return found == entries_.end() ? std::nullopt : std::optional{found->second};
}

void RecordingArchive::Open(Entry& entry, Time now)
{
	if (entry.intervals.empty() || entry.intervals.back().end)
	{
		if (!entry.intervals.empty())
			now = std::max(now, *entry.intervals.back().end);
		entry.intervals.push_back({now, {}});
	}
}

void RecordingArchive::Close(Entry& entry, Time now)
{
	if (!entry.intervals.empty() && !entry.intervals.back().end)
		entry.intervals.back().end = std::max(entry.intervals.back().start, now);
}

RecordingArchive::Entry RecordingArchive::Start(Source source)
{
	std::lock_guard lock(mutex_);
	auto candidate = entries_;
	auto [item, added] = candidate.try_emplace(source.channel);
	auto& entry = item->second;
	if (added)
	{
		entry.token = Token();
		entry.jobToken = Token();
		entry.source = std::move(source);
	}
	else
	{
		if (entry.source.profile != source.profile)
			throw std::invalid_argument("This recording job is bound to a different profile");
		if (entry.active)
			return entry;
	}
	entry.active = true;
	Open(entry, clock_());
	auto result = entry;
	Save(std::move(candidate));
	return result;
}

void RecordingArchive::Stop(std::string_view channel)
{
	std::lock_guard lock(mutex_);
	auto candidate = entries_;
	const auto item = candidate.find(channel);
	if (item == candidate.end() || !item->second.active)
		return;
	item->second.active = false;
	Close(item->second, clock_());
	Save(std::move(candidate));
}

void RecordingArchive::SetJobMode(std::string_view token, bool active, const Available& available)
{
	std::lock_guard lock(mutex_);
	auto candidate = entries_;
	const auto item = std::ranges::find_if(candidate, [&](const auto& pair) { return pair.second.jobToken == token; });
	if (item == candidate.end())
		throw std::out_of_range("Recording job does not exist");
	auto& entry = item->second;
	if (entry.active == active)
		return;
	if (active && !available(entry.source.channel, entry.source.profile))
		throw std::invalid_argument("The recording job source is unavailable");
	entry.active = active;
	if (active)
		Open(entry, clock_());
	else
		Close(entry, clock_());
	Save(std::move(candidate));
}

void RecordingArchive::Suspend()
{
	std::lock_guard lock(mutex_);
	auto candidate = entries_;
	const auto now = clock_();
	bool changed = false;
	for (auto& [_, entry] : candidate)
		if (!entry.intervals.empty() && !entry.intervals.back().end)
		{
			Close(entry, now);
			changed = true;
		}
	if (changed)
		Save(std::move(candidate));
}

void RecordingArchive::Resume(const Available& available)
{
	std::lock_guard lock(mutex_);
	auto candidate = entries_;
	const auto now = clock_();
	bool changed = false;
	for (auto& [channel, entry] : candidate)
		if (entry.active)
		{
			if (available(channel, entry.source.profile))
				Open(entry, now);
			else
				entry.active = false;
			changed = true;
		}
	if (changed)
		Save(std::move(candidate));
}

void RecordingArchive::Save(Entries candidate)
{
	Tree root, recordings;
	for (const auto& [channel, entry] : candidate)
	{
		Tree node, intervals;
		node.put("token", entry.token);
		node.put("job", entry.jobToken);
		node.put("active", entry.active);
		node.put("source.profile", entry.source.profile);
		node.put("source.videoSource", entry.source.videoSource);
		node.put("source.name", entry.source.name);
		node.put("source.address", entry.source.address);
		node.put_child("source.video", entry.source.video);
		if (entry.source.audio)
			node.put_child("source.audio", *entry.source.audio);
		node.put("source.metadata", entry.source.metadata);
		for (const auto& interval : entry.intervals)
		{
			Tree item;
			item.put("start", Ticks(interval.start));
			if (interval.end)
				item.put("end", Ticks(*interval.end));
			intervals.push_back({"", item});
		}
		node.put_child("intervals", intervals);
		recordings.push_back({channel, node});
	}
	root.put_child("recordings", recordings);
	std::ostringstream json;
	boost::property_tree::write_json(json, root);
	utility::media::ConfigsReaderWriter(path_).SaveJson(json.str());
	entries_.swap(candidate);
}
} // namespace osrv
