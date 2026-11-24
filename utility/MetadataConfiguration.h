#pragma once

#include "../onvif/Notification.h"
#include <boost/property_tree/ptree.hpp>
#include <functional>
#include <memory>
#include <set>
#include <stdexcept>

namespace utility::media
{
class MediaProfilesManager;
}
namespace osrv
{
class ServerConfigs;
}
namespace osrv::metadata
{
struct Fault : std::runtime_error
{
	std::string detail;
	Fault(std::string detail, const std::string& reason) : std::runtime_error(reason), detail(std::move(detail))
	{
	}
};
struct Settings
{
	bool events = false;
	bool analytics = false;
	bool tracker = true;
 std::set<std::string> modules{scene::ModuleName, scene::LprModule, scene::FaceModule};
	std::string video_source, analytics_configuration;
	std::string identity;
	std::function<bool(const event::NotificationMessage&, const std::map<std::string, std::string>&)> accepts;
};
// Compiles and validates all filters before publishing an immutable streaming snapshot.
std::shared_ptr<const Settings> Compile(const boost::property_tree::ptree& configuration);
boost::property_tree::ptree ConfigurationXml(const boost::property_tree::ptree& configuration,
																						 const boost::property_tree::ptree& profiles);

class Control
{
public:
	using Configure = std::function<void(const std::string&, const std::string&, bool, std::shared_ptr<const Settings>)>;
	using Retain = std::function<void(const std::vector<std::string>&)>;
	using Synchronize = std::function<void(const std::string&)>;
	Control(utility::media::MediaProfilesManager& manager, const ServerConfigs& server,
					const std::string& configs_path = {});
	void Bind(Configure configure, Synchronize synchronize, Retain retain = {});
	void Refresh();
	boost::property_tree::ptree Execute(const std::string& operation, const std::string& xml, bool media2, bool secure = false);
	std::string Uri(const std::string& profile);

private:
	utility::media::MediaProfilesManager& manager_;
	const ServerConfigs& server_;
	std::map<std::string, std::string> encoders_;
	Configure configure_;
	Synchronize synchronize_;
	Retain retain_;
};
} // namespace osrv::metadata
