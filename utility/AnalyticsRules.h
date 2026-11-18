#pragma once

#include "../onvif_services/pullpoint/pull_point.h"
#include <boost/property_tree/ptree.hpp>
#include <functional>
#include <map>
#include <mutex>
#include <stdexcept>
#include <tuple>

namespace utility
{
class RuleFault : public std::runtime_error
{
public:
	RuleFault(std::string detail, bool receiver = false)
			: std::runtime_error(detail), detail(std::move(detail)), receiver(receiver)
	{
	}
	std::string detail;
	bool receiver;
};

// Rules and event state share a lock; the event thread never accesses the media manager.
class AnalyticsRules
{
public:
	using Tree = boost::property_tree::ptree;
	using Message = osrv::event::NotificationMessage;
	using SaveRules = std::function<void(const std::string&, const Tree&)>;
	AnalyticsRules(const Tree& profiles, SaveRules save);
	Tree Execute(const std::string& operation, const std::string& xml);
	std::deque<Message> Synchronize() const;
	std::deque<Message> Sample(double x, double y, double strength);
	std::deque<Message> SampleObject(const std::string& videoSource, const std::string& objectId, double x, double y);
	void ForgetObject(const std::string& videoSource, const std::string& objectId);
	std::vector<std::string> VideoSources() const;
	static Tree RuleXml(const Tree& rule);
	static constexpr const char* Topic = "tns1:RuleEngine/MotionRegionDetector/Motion";

private:
	Message Notification(const std::string& token, const Tree& rule, const std::string& operation, bool state) const;
	Tree profiles_;
	SaveRules save_;
	mutable std::mutex mutex_;
	std::map<std::pair<std::string, std::string>, bool> states_;
	std::map<std::tuple<std::string, std::string, std::string>, std::pair<double, double>> tracks_;
	std::deque<Message> pending_;
};
} // namespace utility
