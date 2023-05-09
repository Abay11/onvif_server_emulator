#pragma once

#include "IOnvifService.h"

class ILogger;

namespace osrv
{
class RecordingsMgr;

class RecordingSearchService : public IOnvifService
{
public:
	RecordingSearchService(const std::string& service_uri, const std::string& service_name,
												 std::shared_ptr<IOnvifServer> srv);

private:
	std::shared_ptr<RecordingsMgr> rec_mgr_;
};
} // namespace osrv
