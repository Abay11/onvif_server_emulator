#include "LineCounting.h"
#include "AnalyticsRules.h"
#include <algorithm>
#include <boost/algorithm/string/trim.hpp>
#include <cmath>
#include <ctime>
#include <limits>
#include <regex>
#include <set>

namespace utility::counting
{
namespace
{
bool hasText(const Tree& node)
{
	return node.data().find_first_not_of(" \t\r\n") != std::string::npos;
}
std::chrono::microseconds interval(const std::string& value)
{
	// Fixed durations only: the advertised range is zero through one day.
	static const std::regex pattern(
			R"(^P(?:(\d+)Y)?(?:(\d+)M)?(?:(\d+)D)?(?:T(?:(\d+)H)?(?:(\d+)M)?(?:(\d+(?:\.\d+)?)S)?)?$)");
	std::smatch m;
	if (!std::regex_match(value, m, pattern))
		throw RuleFault("InvalidRule");
	try
	{
		bool any = false;
		long double seconds = 0;
		for (int i = 1; i <= 6; ++i)
			if (m[i].matched)
			{
				any = true;
				auto n = std::stold(m[i]);
				if (i <= 2 && n != 0)
					throw RuleFault("InvalidRule");
				seconds += n * (i == 3 ? 86400 : i == 4 ? 3600 : i == 5 ? 60 : i == 6 ? 1 : 0);
			}
		if (!any || (value.find('T') != std::string::npos && !m[4].matched && !m[5].matched && !m[6].matched) ||
				!std::isfinite(seconds) || seconds > 86400)
			throw RuleFault("InvalidRule");
		return std::chrono::microseconds(static_cast<long long>(std::ceil(seconds * 1000000)));
	}
	catch (const std::logic_error&)
	{
		throw RuleFault("InvalidRule");
	}
}
struct Reset
{
	int hour, minute, second;
	std::chrono::microseconds fraction;
	std::optional<int> offset;
};
Reset resetTime(const std::string& value)
{
	static const std::regex pattern(R"(^(\d{2}):(\d{2}):(\d{2})(\.\d+)?(Z|[+-]\d{2}:\d{2})?$)");
	std::smatch m;
	if (!std::regex_match(value, m, pattern))
		throw RuleFault("InvalidRule");
	Reset r{std::stoi(m[1]), std::stoi(m[2]), std::stoi(m[3]), {}, {}};
	long double fraction = 0;
	try
	{
		fraction = m[4].matched ? std::stold(m[4]) : 0;
	}
	catch (const std::logic_error&)
	{
		throw RuleFault("InvalidRule");
	}
	if (r.hour > 24 || r.minute > 59 || r.second > 59 ||
			(r.hour == 24 && (r.minute != 0 || r.second != 0 || fraction != 0)))
		throw RuleFault("InvalidRule");
	r.fraction = std::chrono::microseconds(static_cast<long long>(std::ceil(fraction * 1000000)));
	if (m[5].matched)
	{
		auto zone = m[5].str();
		int hours = zone == "Z" ? 0 : std::stoi(zone.substr(1, 2));
		int minutes = zone == "Z" ? 0 : std::stoi(zone.substr(4, 2));
		if (hours > 14 || minutes > 59 || (hours == 14 && minutes != 0))
			throw RuleFault("InvalidRule");
		r.offset = (hours * 60 + minutes) * (zone.front() == '-' ? -1 : 1);
	}
	return r;
}
bool resetDue(const Tree& rule, Time previous, Time now)
{
	if (now <= previous)
		return false;
	auto raw = std::chrono::system_clock::to_time_t(now);
	std::tm today{};
	localtime_r(&raw, &today);
	for (const auto& [_, time] : rule.get_child("ResetTime"))
	{
		auto reset = resetTime(time.data());
		// Nearby local dates cover explicit offsets and 24:00:00 as well as local schedules.
		for (int day = -2; day <= 1; ++day)
		{
			auto date = today;
			date.tm_mday += day;
			date.tm_hour = reset.hour;
			date.tm_min = reset.minute;
			date.tm_sec = reset.second;
			date.tm_isdst = -1;
			auto stamp = reset.offset ? timegm(&date) - *reset.offset * 60 : std::mktime(&date);
			auto candidate = std::chrono::system_clock::from_time_t(stamp) + reset.fraction;
			if (candidate > previous && candidate <= now)
				return true;
		}
	}
	return false;
}
Tree polyline(const Tree& rule, const Tree& points)
{
	Tree result;
	result.put("Direction", rule.get<std::string>("Direction"));
	result.add_child("Segments", points);
	return result;
}
void simple(Tree& params, const std::string& name, const std::string& value)
{
	Tree item;
	item.put("<xmlattr>.Name", name);
	item.put("<xmlattr>.Value", value);
	params.add_child("tt:SimpleItem", item);
}
} // namespace

Tree ReadRule(const Tree& xml, const Tree* previous)
{
	if (xml.get<std::string>("<xmlattr>.Type") != Type || xml.size() != 2 || xml.count("tt:Parameters") != 1 ||
			xml.get_child("<xmlattr>").size() != 2 || hasText(xml) || hasText(xml.get_child("tt:Parameters")))
		throw RuleFault("InvalidRule");
	Tree result = previous ? *previous : Tree{};
	result.put("Name", xml.get<std::string>("<xmlattr>.Name"));
	result.put("Type", Type);
	if (result.get<std::string>("Name").empty())
		throw RuleFault("InvalidRule");
	if (!previous)
	{
		result.put("Direction", "Any");
		result.put("PassAllPolylines", false);
		result.put("ReportTimeInterval", "PT0S");
		result.add_child("ResetTime", Tree{});
		Tree def;
		def.put("<xmlattr>.Name", "default");
		def.put("<xmlattr>.Type", line::Type);
		def.add_child("tt:Parameters", Tree{});
		Tree segments;
		segments.push_back({"", line::ReadRule(def, nullptr).get_child("Segments")});
		result.add_child("Segments", segments);
	}
	std::set<std::string> seen;
	bool elements = false;
	Tree segments, resets;
	for (const auto& [key, item] : xml.get_child("tt:Parameters"))
	{
		if (hasText(item))
			throw RuleFault("InvalidRule");
		auto name = item.get<std::string>("<xmlattr>.Name");
		if (key == "tt:SimpleItem" && !elements && item.size() == 1 && item.get_child("<xmlattr>").size() == 2)
		{
			auto value = item.get<std::string>("<xmlattr>.Value");
			if (name == "ReportTimeInterval" || name == "ResetTime" || name == "PassAllPolylines")
				boost::algorithm::trim(value);
			if (name != "ResetTime" && !seen.insert(name).second)
				throw RuleFault("InvalidRule");
			if (name == "Direction")
			{
				if (value != "Any" && value != "Left" && value != "Right")
					throw RuleFault("InvalidRule");
				result.put(name, value);
			}
			else if (name == "PassAllPolylines")
			{
				if (value != "true" && value != "false" && value != "1" && value != "0")
					throw RuleFault("InvalidRule");
				result.put(name, value == "true" || value == "1");
			}
			else if (name == "ReportTimeInterval")
			{
				interval(value);
				result.put(name, value);
			}
			else if (name == "ResetTime")
			{
				resetTime(value);
				Tree time;
				time.put_value(value);
				resets.push_back({"", time});
			}
			else
				throw RuleFault("InvalidRule");
		}
		else if (key == "tt:ElementItem" && name == "Segments")
		{
			elements = true;
			Tree wrapped;
			wrapped.put("<xmlattr>.Name", "validation");
			wrapped.put("<xmlattr>.Type", line::Type);
			wrapped.add_child("tt:Parameters.tt:ElementItem", item);
			segments.push_back({"", line::ReadRule(wrapped, nullptr).get_child("Segments")});
		}
		else
			throw RuleFault("InvalidRule");
	}
	if (segments.size() > 4)
		throw RuleFault("InvalidRule");
	if (!segments.empty())
		result.put_child("Segments", segments);
	if (!resets.empty())
		result.put_child("ResetTime", resets);
	return result;
}

Tree RuleXml(const Tree& rule)
{
	if (rule.get_child("Segments").empty() || rule.get_child("Segments").size() > 4)
		throw RuleFault("InvalidRule");
	Tree result, params;
	result.put("<xmlattr>.Name", rule.get<std::string>("Name"));
	result.put("<xmlattr>.Type", Type);
	simple(params, "ReportTimeInterval", rule.get<std::string>("ReportTimeInterval"));
	for (const auto& [_, time] : rule.get_child("ResetTime"))
		simple(params, "ResetTime", time.data());
	simple(params, "Direction", rule.get<std::string>("Direction"));
	simple(params, "PassAllPolylines", rule.get<bool>("PassAllPolylines") ? "true" : "false");
	for (const auto& [_, points] : rule.get_child("Segments"))
	{
		auto line = polyline(rule, points);
		line.put("Name", "serialization");
		params.add_child("tt:ElementItem", line::RuleXml(line).get_child("tt:Parameters.tt:ElementItem"));
	}
	result.add_child("tt:Parameters", params);
	return result;
}

Tree Description()
{
	auto desc = line::Description();
	desc.put("<xmlattr>.Name", Type);
	Tree params;
	for (auto [name, type] : {std::pair{"ReportTimeInterval", "xs:duration"},
														{"ResetTime", "xs:time"},
														{"Direction", "tt:Direction"},
														{"PassAllPolylines", "xs:boolean"}})
	{
		Tree item;
		item.put("<xmlattr>.Name", name);
		item.put("<xmlattr>.Type", type);
		params.add_child("tt:SimpleItemDescription", item);
	}
	params.put_child("tt:ElementItemDescription", desc.get_child("tt:Parameters.tt:ElementItemDescription"));
	desc.put_child("tt:Parameters", params);
	desc.put("tt:Messages.<xmlattr>.IsProperty", true);
	desc.put("tt:Messages.tt:Data.tt:SimpleItemDescription.<xmlattr>.Name", "Count");
	desc.put("tt:Messages.tt:Data.tt:SimpleItemDescription.<xmlattr>.Type", "xs:int");
	desc.put("tt:Messages.tt:ParentTopic", Topic);
	return desc;
}

Tree Options()
{
	auto result = line::Options();
	for (auto& [_, option] : result)
	{
		option.put("<xmlattr>.RuleType", Type);
		if (option.get<std::string>("<xmlattr>.Name") == "Segments")
			option.put("<xmlattr>.maxOccurs", 4);
	}
	Tree report;
	report.put("<xmlattr>.Name", "ReportTimeInterval");
	report.put("<xmlattr>.RuleType", Type);
	report.put("<xmlattr>.minOccurs", 0);
	report.put("<xmlattr>.maxOccurs", 1);
	report.put("tt:DurationRange.tt:Min", "PT0S");
	report.put("tt:DurationRange.tt:Max", "P1D");
	result.add_child("tan:RuleOptions", report);
	return result;
}

bool State::Advance(const Tree& rule, Time now)
{
	bool notify = false;
	if (resetDue(rule, last_tick_, now))
	{
		notify = count != 0 || dirty_;
		count = 0;
		tracks_.clear();
		dirty_ = false;
		last_report_ = now;
	}
	if (now < last_tick_)
		last_report_ = now;
	last_tick_ = now;
	if (dirty_ && now - last_report_ >= interval(rule.get<std::string>("ReportTimeInterval")))
	{
		notify = true;
		dirty_ = false;
		last_report_ = now;
	}
	return notify;
}

bool State::Sample(const Tree& rule, const std::string& id, line::Point point, Time now)
{
	bool notify = Advance(rule, now);
	auto& track = tracks_[id];
	const auto& polylines = rule.get_child("Segments");
	track.previous.resize(polylines.size());
	track.passed.resize(polylines.size());
	int crossings = 0;
	size_t i = 0;
	for (const auto& [_, points] : polylines)
	{
		auto line = polyline(rule, points);
		if (!line::OnLine(line, point))
		{
			if (track.previous[i] && line::Crossed(line, *track.previous[i], point))
			{
				++crossings;
				track.passed[i] = true;
			}
			track.previous[i] = point;
		}
		++i;
	}
	if (rule.get<bool>("PassAllPolylines"))
	{
		crossings = std::all_of(track.passed.begin(), track.passed.end(), [](bool v) { return v; }) ? 1 : 0;
		if (crossings)
			std::fill(track.passed.begin(), track.passed.end(), false);
	}
	if (crossings && count < std::numeric_limits<int>::max())
	{
		count += std::min(crossings, std::numeric_limits<int>::max() - count);
		dirty_ = true;
	}
	if (dirty_ && now - last_report_ >= interval(rule.get<std::string>("ReportTimeInterval")))
	{
		notify = true;
		dirty_ = false;
		last_report_ = now;
	}
	// A due report and this sample share one notification carrying the final count.
	if (notify)
	{
		dirty_ = false;
		last_report_ = now;
	}
	return notify;
}
} // namespace utility::counting
