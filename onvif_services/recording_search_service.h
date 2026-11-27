#pragma once

#include "IOnvifService.h"

namespace osrv
{
class RecordingSearchService : public IOnvifService
{
public:
	RecordingSearchService(const std::string& service_uri, const std::string& service_name,
												 std::shared_ptr<IOnvifServer> srv);

};
} // namespace osrv
