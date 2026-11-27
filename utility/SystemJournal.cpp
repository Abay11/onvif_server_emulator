#include "SystemJournal.h"
#include "../onvif/Notification.h"
#include "DateTime.hpp"
#include <algorithm>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <cctype>
#include <stdexcept>

namespace osrv
{
namespace
{
std::string Clip(const std::string& text, std::size_t maximum)
{
	if (text.size() <= maximum)
		return text;
	auto end = maximum;
	while (end && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80)
		--end;
	return text.substr(0, end);
}
std::string Lower(std::string value)
{
	for (auto& ch : value)
		ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
	return value;
}
} // namespace
SystemJournal::SystemJournal(std::size_t capacity)
		: capacity_(capacity), instance_(boost::uuids::to_string(boost::uuids::random_generator()()))
{
	if (!capacity || capacity > 65536)
		throw std::invalid_argument("Journal capacity must be 1..65536");
}
void SystemJournal::Append(Entry entry)
{
	entry.id = next_++;
	if (entries_.size() == capacity_)
		entries_.pop_front();
	entries_.push_back(std::move(entry));
}
void SystemJournal::Add(std::string level, std::string category, std::string source, std::string message)
{
	Entry entry{0,
							utility::datetime::system_utc_datetime(),
							Clip(level, 16),
							Clip(category, 32),
							Clip(source, 256),
							Clip(message, 1024)};
	std::lock_guard lock(mutex_);
	Append(std::move(entry));
}
void SystemJournal::Client(const std::string& protocol, const std::string& peer, const std::string& identity)
{
	const auto source = Clip(protocol, 32);
	const auto detail = Clip(peer, 96) + " (" + Clip(identity, 48) + ")";
	const auto key = source + ":" + detail;
	const auto now = std::chrono::steady_clock::now();
	std::lock_guard lock(mutex_);
	auto found = clients_.find(key);
	if (found != clients_.end() && now - found->second < std::chrono::minutes(1))
		return;
	if (found == clients_.end() && clients_.size() >= 256)
	{
		auto oldest = std::min_element(clients_.begin(), clients_.end(),
																	 [](const auto& a, const auto& b) { return a.second < b.second; });
		clients_.erase(oldest);
	}
	clients_[key] = now;
	Append({0, utility::datetime::system_utc_datetime(), "info", "client", source, "Client activity: " + detail});
}
void SystemJournal::Detector(const event::NotificationMessage& event)
{
	// Keep summaries independent of XML serialization; never copy image payloads,
	// arbitrary ElementItems, credentials, face labels or licence plate images.
	std::string summary = event.property_operation.empty() ? "Detection" : Clip(event.property_operation, 32);
	unsigned count = 0;
	if (!event.video_source.empty())
		summary += " · VideoSource=" + Clip(event.video_source, 96);
	for (const auto& [name, value] : event.source_item_descriptions)
		if ((name == "Rule" || name == "RuleName" || name == "InputToken" || name == "VideoSource" ||
				 name == "VideoSourceConfigurationToken") &&
				count++ < 6)
			summary += " · " + name + "=" + Clip(value, 96);
	auto data = [&](const std::string& name, const std::string& value) {
		static const std::vector<std::string> allowed{
				"State",		"LogicalState", "IsMotion", "isSoundDetected", "ObjectId",	 "Count",			"CountIn",
				"CountOut", "CurrentCount", "IsInside", "Occupancy",			 "Likelihood", "Direction", "Speed"};
		if (std::find(allowed.begin(), allowed.end(), name) != allowed.end() && count++ < 12)
			summary += " · " + name + "=" + Clip(value, 96);
	};
	data(event.data_name, event.data_value);
	for (const auto& [name, value] : event.data_simple_items)
		data(name, value);
	Add("info", "detector", event.topic, summary);
}
SystemJournal::Batch SystemJournal::Read(std::uint64_t after, std::size_t limit, const Filter& filter) const
{
	if (!limit || limit > 200)
		throw std::invalid_argument("Journal limit must be 1..200");
	const auto query = Lower(Clip(filter.text, 256));
	std::lock_guard lock(mutex_);
	Batch batch;
	const auto latest = next_ - 1;
	if (after > latest)
	{
		after = 0;
		batch.reset = true;
	}
	batch.cursor = after;
	if (!entries_.empty() && after < entries_.front().id - 1)
		batch.dropped = entries_.front().id - after - 1;
	for (const auto& entry : entries_)
	{
		if (entry.id <= after)
			continue;
		batch.cursor = entry.id;
		if ((!filter.category.empty() && entry.category != filter.category) ||
				(!filter.level.empty() && entry.level != filter.level))
			continue;
		if (!query.empty() && Lower(entry.source + " " + entry.message).find(query) == std::string::npos)
			continue;
		batch.entries.push_back(entry);
		if (batch.entries.size() == limit)
			break;
	}
	batch.more = batch.cursor < latest;
	return batch;
}
} // namespace osrv
