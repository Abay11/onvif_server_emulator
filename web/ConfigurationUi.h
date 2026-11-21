#pragma once
#include <boost/property_tree/ptree.hpp>
#include <map>
#include <mutex>
#include <string>

namespace osrv
{
class IOnvifServer;
// The HTTP loop owns live media mutations. Other files are saved for restart.
class ConfigurationUi
{
public:
	explicit ConfigurationUi(IOnvifServer& server);
	std::string Read();
	std::string Change(const boost::property_tree::ptree& command);

private:
	struct Snapshot
	{
		std::string bytes, revision;
	};
	IOnvifServer& server_;
	std::mutex mutex_;
	std::map<std::string, Snapshot> snapshots_;
	bool restart_required_ = false;
};
} // namespace osrv
