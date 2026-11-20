#include "DetectorControls.h"
#include "../onvif/Notification.h"
#include "AnalyticsRules.h"
#include "FieldDetector.h"
#include "LineDetector.h"
#include <stdexcept>

namespace osrv
{
DetectorControls::DetectorControls()
{
	Register("tt:MotionRegionDetector", utility::AnalyticsRules::Topic, true);
	Register(utility::line::Type, utility::line::Topic, true);
	Register(utility::counting::Type, utility::counting::Topic, true);
	Register(utility::field::Type, utility::field::Topic, true);
	Register(utility::lpr::Type, utility::lpr::Topic, true);
	Register(utility::face::Type, utility::face::Topic, true);
}
void DetectorControls::Register(const std::string& id, const std::string& topic, bool enabled)
{
	std::lock_guard lock(mutex_);
	enabled_.try_emplace(id, enabled);
	topics_[topic] = id;
}
void DetectorControls::Set(const std::string& id, bool enabled)
{
	std::lock_guard lock(mutex_);
	auto it = enabled_.find(id);
	if (it == enabled_.end())
		throw std::invalid_argument("Unknown detector");
	it->second = enabled;
}
bool DetectorControls::Enabled(const std::string& id) const
{
	std::lock_guard lock(mutex_);
	auto it = enabled_.find(id);
	return it == enabled_.end() || it->second;
}
void DetectorControls::SetRule(const std::string& configuration, const std::string& name, bool enabled)
{
	std::lock_guard lock(mutex_);
	if (enabled)
		disabled_rules_.erase({configuration, name});
	else
		disabled_rules_.emplace(configuration, name);
}
bool DetectorControls::RuleEnabled(const std::string& configuration, const std::string& name) const
{
	std::lock_guard lock(mutex_);
	return !disabled_rules_.contains({configuration, name});
}
void DetectorControls::ForgetRule(const std::string& configuration, const std::string& name)
{
	SetRule(configuration, name, true);
}
bool DetectorControls::Allows(const event::NotificationMessage& message) const
{
	std::lock_guard lock(mutex_);
	auto type = topics_.find(message.topic);
	if (type != topics_.end())
	{
		auto enabled = enabled_.find(type->second);
		if (enabled != enabled_.end() && !enabled->second)
			return false;
	}
	if (!message.analytics_configuration.empty())
	{
		if (!enabled_.at("rules"))
			return false;
		for (const auto& [key, value] : message.source_item_descriptions)
			if ((key == "Rule" || key == "RuleName") && disabled_rules_.contains({message.analytics_configuration, value}))
				return false;
	}
	return true;
}
} // namespace osrv
