#pragma once

#include <boost/property_tree/ptree.hpp>

#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace osrv
{
class RecordingArchive
{
public:
	using Time = std::chrono::system_clock::time_point;
	using Clock = std::function<Time()>;
	using Available = std::function<bool(const std::string&, const std::string&)>;
	using Tree = boost::property_tree::ptree;

	struct Source
	{
		std::string channel, profile, videoSource, name, address;
		Tree video;
		std::optional<Tree> audio;
		bool metadata = false;
	};
	struct Interval
	{
		Time start;
		std::optional<Time> end;
	};
	struct Entry
	{
		std::string token, jobToken;
		Source source;
		bool active = false;
		std::vector<Interval> intervals;
	};

	explicit RecordingArchive(std::string path, Clock clock = std::chrono::system_clock::now);
	std::vector<Entry> Snapshot() const;
	std::optional<Entry> Recording(std::string_view token) const;
	std::optional<Entry> Job(std::string_view token) const;
	Entry Start(Source source);
	void Stop(std::string_view channel);
	void SetJobMode(std::string_view token, bool active, const Available& available);
	void Suspend();
	void Resume(const Available& available);
	Time Now() const
	{
		return clock_();
	}

private:
	using Entries = std::map<std::string, Entry, std::less<>>;
	void Save(Entries candidate);
	static void Close(Entry& entry, Time now);
	static void Open(Entry& entry, Time now);

	const std::string path_;
	const Clock clock_;
	mutable std::mutex mutex_;
	Entries entries_;
};
std::string RecordingUtc(RecordingArchive::Time time);
std::vector<std::string> RecordingTracks(const RecordingArchive::Source& source);
RecordingArchive::Tree RecordingSourceXml(const RecordingArchive::Source& source);
} // namespace osrv
