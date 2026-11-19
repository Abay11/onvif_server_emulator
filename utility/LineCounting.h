#pragma once

#include "LineDetector.h"
#include <chrono>
#include <map>
#include <optional>
#include <vector>

namespace utility::counting
{
using Tree = boost::property_tree::ptree;
using Time = std::chrono::system_clock::time_point;
inline constexpr const char* Type = "tt:LineCounting";
inline constexpr const char* Topic = "tns1:RuleEngine/CountAggregation/Counter";
Tree ReadRule(const Tree& xml, const Tree* previous);
Tree RuleXml(const Tree& rule);
Tree Description();
Tree Options();

// The caller holds the shared Rules lock. Counts are transient; configuration is persisted separately.
class State
{
public:
	explicit State(Time now, int count = 0) : count(count), last_tick_(now), last_report_(now)
	{
	}
	bool Advance(const Tree& rule, Time now);
	bool Sample(const Tree& rule, const std::string& id, line::Point point, Time now);
	void Forget(const std::string& id)
	{
		tracks_.erase(id);
	}
	int count;

private:
	struct Track
	{
		std::vector<std::optional<line::Point>> previous;
		std::vector<bool> passed;
	};
	std::map<std::string, Track> tracks_;
	Time last_tick_, last_report_;
	bool dirty_ = false;
};
} // namespace utility::counting
