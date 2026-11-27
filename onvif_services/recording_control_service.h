#pragma once

#include "IOnvifService.h"

namespace osrv
{
class RecordingControlService : public IOnvifService
{
public:
	RecordingControlService(const std::string& uri, const std::string& name, std::shared_ptr<IOnvifServer> server);
};
} // namespace osrv
