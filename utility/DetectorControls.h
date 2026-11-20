#pragma once
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <utility>

namespace osrv::event
{
struct NotificationMessage;
}
namespace osrv
{
// Runtime simulation controls, independent of ONVIF rule parameter schemas.
class DetectorControls
{
public:
	DetectorControls();
	void Register(const std::string& id, const std::string& topic, bool enabled);
	void Set(const std::string& id, bool enabled);
	bool Enabled(const std::string& id) const;
	void SetRule(const std::string& configuration, const std::string& name, bool enabled);
	bool RuleEnabled(const std::string& configuration, const std::string& name) const;
	void ForgetRule(const std::string& configuration, const std::string& name);
	bool Allows(const event::NotificationMessage& message) const;

private:
	mutable std::mutex mutex_;
	std::map<std::string, bool> enabled_{{"rules", true}};
	std::map<std::string, std::string> topics_;
	std::set<std::pair<std::string, std::string>> disabled_rules_;
};
} // namespace osrv
