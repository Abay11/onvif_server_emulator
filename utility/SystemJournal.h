#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace osrv::event
{
struct NotificationMessage;
}
namespace osrv
{
// Independent application history: no ONVIF subscription, timer or transport.
// All entries and client bookkeeping are bounded; readers never consume history.
class SystemJournal
{
public:
	struct Entry
	{
		std::uint64_t id;
		std::string time, level, category, source, message;
	};
	struct Filter
	{
		std::string category, level, text;
	};
	struct Batch
	{
		std::vector<Entry> entries;
		std::uint64_t cursor = 0, dropped = 0;
		bool more = false, reset = false;
	};
	explicit SystemJournal(std::size_t capacity = 4096);
	void Add(std::string level, std::string category, std::string source, std::string message);
	void Detector(const event::NotificationMessage& message);
	// Records HTTP activity, not a fictitious login/session. Repeated activity
	// from the same peer/protocol/role is coalesced for one minute.
	void Client(const std::string& protocol, const std::string& peer, const std::string& identity);
	Batch Read(std::uint64_t after, std::size_t limit, const Filter& filter) const;
	const std::string& Instance() const
	{
		return instance_;
	}

private:
	void Append(Entry entry);
	const std::size_t capacity_;
	const std::string instance_;
	mutable std::mutex mutex_;
	std::deque<Entry> entries_;
	std::uint64_t next_ = 1;
	std::map<std::string, std::chrono::steady_clock::time_point> clients_;
};
} // namespace osrv
