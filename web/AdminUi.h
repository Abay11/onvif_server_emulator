#pragma once

namespace osrv
{
class IOnvifServer;
// Registers the embedded administration page and JSON API on the ONVIF listener.
void RegisterAdminUi(IOnvifServer& server);
} // namespace osrv
