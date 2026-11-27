#pragma once
#include <boost/property_tree/ptree.hpp>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
namespace utility
{
class AnalyticsRules;
}
namespace osrv
{
class IOnvifServer;
struct ServerConfigs;
class DetectorUi
{
public:
	explicit DetectorUi(IOnvifServer& server);
	std::string Read() const;
	void Change(const boost::property_tree::ptree& command);

private:
	mutable std::mutex mutex_;
	std::string events_path_;
	std::shared_ptr<utility::AnalyticsRules> rules_;
	std::shared_ptr<ServerConfigs> configs_;
	boost::property_tree::ptree events_;
	std::vector<std::string> channel_sources_;
};
} // namespace osrv
