#pragma once

#include "IOnvifService.h"

#include "../HttpServerFwd.h"

#include <string>

class ILogger;

namespace utility::media { class MediaProfilesManager; }

namespace osrv
{
namespace ptz
{
void fillPtzConfig(const pt::ptree& config, pt::ptree& response, const utility::media::MediaProfilesManager& manager);
}
class PTZService : public IOnvifService
{
public:
	PTZService(const std::string& service_uri, const std::string& service_name, std::shared_ptr<IOnvifServer> srv);
};
} // namespace osrv